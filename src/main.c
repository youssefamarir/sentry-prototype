// Sensor Node v2: animated gauges + sensor task + Ethernet + network info page.
// Display (SPI3): CLK=36 MOSI=35 CS=34 DC=33 RST=37 BL=21
// Touch  (I2C0):  SDA=47 SCL=48 INT=38 RST=39   CST816S @0x15
// Sensor (I2C1):  SDA=1  SCL=2                   BME688  @0x77
// Ethernet(SPI2): SCLK=13 MISO=12 MOSI=11 CS=14 INT=10 RST=9  W5500

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mqtt_client.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "lvgl.h"
#include "bme68x.h"
#include "esp_http_server.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include <time.h>
#include "web_page.h"

static const char *TAG="node";

// ---- WiFi / MQTT broker / weather API key ----
// Kept out of git. Copy src/secrets.example.h to src/secrets.h and fill it in.
#include "secrets.h"

// ---- MQTT / hub config ----
// NODE_ID must be unique per node (use "closet2" on the second one).
#define NODE_ID    "closet1"
#define MQTT_TOPIC  "sentry/" NODE_ID "/data"
#define MQTT_STATUS "sentry/" NODE_ID "/status"

// ---- display ----
#define PIN_CLK 36
#define PIN_MOSI 35
#define PIN_CS 34
#define PIN_DC 33
#define PIN_RST 37
#define PIN_BL 21
#define H_RES 240
#define V_RES 240
static spi_device_handle_t spi;

// ---- touch ----
#define TP_SDA 47
#define TP_SCL 48
#define TP_INT 38
#define TP_RST 39
#define CST816S_ADDR 0x15
static i2c_master_bus_handle_t tp_bus;
static i2c_master_dev_handle_t tp_dev;

// ---- sensor ----
#define BME_SDA 1
#define BME_SCL 2
#define BME_ADDR 0x77
static i2c_master_bus_handle_t bme_bus;
static i2c_master_dev_handle_t bme_dev;
static struct bme68x_dev bme;
static struct bme68x_conf bme_conf;
static struct bme68x_heatr_conf bme_heat;
static volatile bool sensor_ok=false;

// shared sensor values (written by sensor task, read by UI)
static volatile float s_temp=0, s_hum=0, s_pres=0, s_gas=0;
static volatile bool s_valid=false;

// ---- US unit helpers ----
static inline float C2F(float c){ return c*9.0f/5.0f+32.0f; }
static inline float hpa2inHg(float h){ return h*0.0295299830714f; }
static int air_index(float kohm);   // fwd decl

// ---- 30-day history (15-min samples in NVS ring buffer) ----
#define HIST_SAMPLES 2880          // 30 days * 96 samples/day
#define HIST_INTERVAL_MS (15*60*1000)
typedef struct { int16_t t,h,p,a; } hist_pt;  // t=F*10, h=%, p=inHg*100, a=AQI
static hist_pt hist[HIST_SAMPLES];
static int hist_head=0;            // next write index
static int hist_count=0;           // how many valid
static int64_t last_hist_ms=0;

// ---- trend detection (rolling window of recent live samples) ----
#define TREND_WIN 20               // last 20 live samples (~40s at 2s)
static float trend_temp[TREND_WIN], trend_hum[TREND_WIN], trend_pres[TREND_WIN];
static int trend_idx=0, trend_n=0;
static char predict_msg[64]="";    // current predictive warning

// ---- ethernet (W5500) ----
#define ETH_CLK 13
#define ETH_MISO 12
#define ETH_MOSI 11
#define ETH_CS 14
#define ETH_INT 10
#define ETH_RST 9
static esp_netif_t *eth_netif=NULL;
// network info strings (filled by event handlers)
static char net_ip[20]="---", net_mask[20]="---", net_gw[20]="---";
static char net_mac[20]="---", net_dns[20]="---";
static volatile bool net_link=false, net_gotip=false;

// ---- wifi state ----
static esp_netif_t *wifi_netif=NULL;
static volatile bool wifi_conn=false;
static char wifi_ssid[40]="---";
static char wifi_bssid[20]="---";
static char wifi_ip[20]="---";
static int  wifi_rssi=0;
static int  wifi_chan=0;
static char wifi_auth[16]="---";

// ---- switch discovery (LLDP / CDP / FDP) ----
static char sw_proto[12]="---";   // which protocol gave data
static char sw_name[40]="Listening...";
static char sw_port[40]="---";
static char sw_vlan[16]="---";
static char sw_vmode[24]="---";   // Access(untagged)/Trunk(tagged)
static int  sw_vlan_count=0;
static char sw_mgmt[20]="---";    // mgmt IP if provided
static volatile bool sw_found=false;
static esp_eth_handle_t g_eth=NULL;

// ---- web server ----
static httpd_handle_t g_httpd=NULL;
static int g_ws_fd=-1;            // current websocket client fd (single client)
static int64_t g_boot_us=0;

// on-screen clock label (top layer, always visible)
static lv_obj_t *clock_label=NULL;

// ---- weather (Orlando) ----
#define OWM_LAT      "28.54"
#define OWM_LON      "-81.38"
static char wx_cond[16]="";     // Clear/Clouds/Rain/...
static int  wx_temp=-999;       // deg F
static volatile bool wx_valid=false;
static lv_obj_t *wx_icon=NULL;     // home page weather icon container
static lv_obj_t *wx_temp_lbl=NULL; // home page temp
static lv_obj_t *wx_cond_lbl=NULL; // home page condition text
static lv_obj_t *home_clock=NULL;  // big time on home page

static void lv_tick_task(void *a){ (void)a; lv_tick_inc(1); }

// ================= DISPLAY =================
static void cs(int l){ gpio_set_level(PIN_CS,l); }
static void dc(int l){ gpio_set_level(PIN_DC,l); }
static void spi_send(const uint8_t*d,int n){ if(!n)return; spi_transaction_t t={0}; t.length=n*8; t.tx_buffer=d; spi_device_polling_transmit(spi,&t); }
static void wr_cmd(uint8_t c){ dc(0); cs(0); spi_send(&c,1); cs(1); }
static void wr_data(uint8_t d){ dc(1); cs(0); spi_send(&d,1); cs(1); }
static void panel_reset(void){
    gpio_set_level(PIN_RST,1); vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST,0); vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST,1); vTaskDelay(pdMS_TO_TICKS(150));
}
static void init_reg(void){
    wr_cmd(0xEF); wr_cmd(0xEB); wr_data(0x14); wr_cmd(0xFE); wr_cmd(0xEF);
    wr_cmd(0xEB); wr_data(0x14); wr_cmd(0x84); wr_data(0x40); wr_cmd(0x85); wr_data(0xFF);
    wr_cmd(0x86); wr_data(0xFF); wr_cmd(0x87); wr_data(0xFF); wr_cmd(0x88); wr_data(0x0A);
    wr_cmd(0x89); wr_data(0x21); wr_cmd(0x8A); wr_data(0x00); wr_cmd(0x8B); wr_data(0x80);
    wr_cmd(0x8C); wr_data(0x01); wr_cmd(0x8D); wr_data(0x01); wr_cmd(0x8E); wr_data(0xFF);
    wr_cmd(0x8F); wr_data(0xFF); wr_cmd(0xB6); wr_data(0x00); wr_data(0x20);
    wr_cmd(0x36); wr_data(0x08); wr_cmd(0x3A); wr_data(0x05);
    wr_cmd(0x90); wr_data(0x08); wr_data(0x08); wr_data(0x08); wr_data(0x08);
    wr_cmd(0xBD); wr_data(0x06); wr_cmd(0xBC); wr_data(0x00);
    wr_cmd(0xFF); wr_data(0x60); wr_data(0x01); wr_data(0x04);
    wr_cmd(0xC3); wr_data(0x13); wr_cmd(0xC4); wr_data(0x13); wr_cmd(0xC9); wr_data(0x22);
    wr_cmd(0xBE); wr_data(0x11); wr_cmd(0xE1); wr_data(0x10); wr_data(0x0E);
    wr_cmd(0xDF); wr_data(0x21); wr_data(0x0c); wr_data(0x02);
    wr_cmd(0xF0); wr_data(0x45); wr_data(0x09); wr_data(0x08); wr_data(0x08); wr_data(0x26); wr_data(0x2A);
    wr_cmd(0xF1); wr_data(0x43); wr_data(0x70); wr_data(0x72); wr_data(0x36); wr_data(0x37); wr_data(0x6F);
    wr_cmd(0xF2); wr_data(0x45); wr_data(0x09); wr_data(0x08); wr_data(0x08); wr_data(0x26); wr_data(0x2A);
    wr_cmd(0xF3); wr_data(0x43); wr_data(0x70); wr_data(0x72); wr_data(0x36); wr_data(0x37); wr_data(0x6F);
    wr_cmd(0xED); wr_data(0x1B); wr_data(0x0B); wr_cmd(0xAE); wr_data(0x77); wr_cmd(0xCD); wr_data(0x63);
    wr_cmd(0x70); wr_data(0x07); wr_data(0x07); wr_data(0x04); wr_data(0x0E); wr_data(0x0F); wr_data(0x09); wr_data(0x07); wr_data(0x08); wr_data(0x03);
    wr_cmd(0xE8); wr_data(0x34);
    wr_cmd(0x62); wr_data(0x18); wr_data(0x0D); wr_data(0x71); wr_data(0xED); wr_data(0x70); wr_data(0x70); wr_data(0x18); wr_data(0x0F); wr_data(0x71); wr_data(0xEF); wr_data(0x70); wr_data(0x70);
    wr_cmd(0x63); wr_data(0x18); wr_data(0x11); wr_data(0x71); wr_data(0xF1); wr_data(0x70); wr_data(0x70); wr_data(0x18); wr_data(0x13); wr_data(0x71); wr_data(0xF3); wr_data(0x70); wr_data(0x70);
    wr_cmd(0x64); wr_data(0x28); wr_data(0x29); wr_data(0xF1); wr_data(0x01); wr_data(0xF1); wr_data(0x00); wr_data(0x07);
    wr_cmd(0x66); wr_data(0x3C); wr_data(0x00); wr_data(0xCD); wr_data(0x67); wr_data(0x45); wr_data(0x45); wr_data(0x10); wr_data(0x00); wr_data(0x00); wr_data(0x00);
    wr_cmd(0x67); wr_data(0x00); wr_data(0x3C); wr_data(0x00); wr_data(0x00); wr_data(0x00); wr_data(0x01); wr_data(0x54); wr_data(0x10); wr_data(0x32); wr_data(0x98);
    wr_cmd(0x74); wr_data(0x10); wr_data(0x85); wr_data(0x80); wr_data(0x00); wr_data(0x00); wr_data(0x4E); wr_data(0x00);
    wr_cmd(0x98); wr_data(0x3e); wr_data(0x07);
    wr_cmd(0x35); wr_cmd(0x21);
    wr_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    wr_cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));
}
static void set_window(int x0,int y0,int x1,int y1){
    wr_cmd(0x2A); wr_data(x0>>8); wr_data(x0); wr_data(x1>>8); wr_data(x1);
    wr_cmd(0x2B); wr_data(y0>>8); wr_data(y0); wr_data(y1>>8); wr_data(y1);
    wr_cmd(0x2C);
}
static lv_disp_draw_buf_t draw_buf;
#define BUF_LINES 60
static lv_color_t buf1[H_RES*BUF_LINES];
static lv_color_t buf2[H_RES*BUF_LINES];
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *cp){
    int x1=area->x1,y1=area->y1,x2=area->x2,y2=area->y2;
    set_window(x1,y1,x2,y2);
    int n=(x2-x1+1)*(y2-y1+1);
    dc(1); cs(0); spi_send((const uint8_t*)cp,n*2); cs(1);
    lv_disp_flush_ready(drv);
}

// ================= TOUCH =================
static void tp_reset(void){
    gpio_set_level(TP_RST,0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TP_RST,1); vTaskDelay(pdMS_TO_TICKS(100));
}
static bool tp_read_raw(uint16_t*x,uint16_t*y){
    // CST816S: read from reg 0x00; byte[2] = finger count, byte[3..6] = X/Y
    uint8_t reg=0x00, d[7]={0};
    if(i2c_master_transmit_receive(tp_dev,&reg,1,d,7,50)!=ESP_OK) return false;
    uint8_t fingers = d[2] & 0x0F;
    if(fingers==0) return false;              // no real touch
    uint16_t rx = ((d[3]&0x0F)<<8) | d[4];
    uint16_t ry = ((d[5]&0x0F)<<8) | d[6];
    if(rx>=H_RES || ry>=V_RES) return false;  // reject out-of-range garbage
    *x=rx; *y=ry;
    return true;
}
static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data){
    (void)drv; uint16_t x,y;
    static uint16_t lx=0, ly=0;
    if(tp_read_raw(&x,&y)){
        lx=x; ly=y;
        data->point.x=x; data->point.y=y; data->state=LV_INDEV_STATE_PRESSED;
    } else {
        data->point.x=lx; data->point.y=ly;   // hold last point on release
        data->state=LV_INDEV_STATE_RELEASED;
    }
}

// ================= BME688 + sensor task =================
static BME68X_INTF_RET_TYPE bme_rd(uint8_t reg,uint8_t*d,uint32_t len,void*p){ (void)p; return i2c_master_transmit_receive(bme_dev,&reg,1,d,len,1000)==ESP_OK?0:-1; }
static BME68X_INTF_RET_TYPE bme_wr(uint8_t reg,const uint8_t*d,uint32_t len,void*p){ (void)p; uint8_t b[64]; if(len+1>sizeof(b))return -1; b[0]=reg; memcpy(&b[1],d,len); return i2c_master_transmit(bme_dev,b,len+1,1000)==ESP_OK?0:-1; }
static void bme_us(uint32_t pe,void*p){ (void)p; esp_rom_delay_us(pe); }

static void sensor_task(void *arg){
    (void)arg;
    struct bme68x_data data;
    while(1){
        if(sensor_ok){
            bme68x_set_op_mode(BME68X_FORCED_MODE,&bme);
            uint32_t del=bme68x_get_meas_dur(BME68X_FORCED_MODE,&bme_conf,&bme)+(bme_heat.heatr_dur*1000);
            bme.delay_us(del,bme.intf_ptr);
            uint8_t n=0;
            if(bme68x_get_data(BME68X_FORCED_MODE,&data,&n,&bme)==BME68X_OK && n>0){
                s_temp=data.temperature; s_hum=data.humidity;
                s_pres=data.pressure/100.0f; s_gas=data.gas_resistance/1000.0f;
                s_valid=true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ================= ETHERNET =================
// ===== Discovery frame parsing (LLDP 0x88CC, CDP/FDP via SNAP) =====
static void str_copy(char*dst,size_t cap,const uint8_t*src,int len){
    if(len<0) len=0;
    if((size_t)len>cap-1) len=cap-1;
    memcpy(dst,src,len); dst[len]=0;
}

// Parse LLDP TLVs. Returns true if recognized.
static bool parse_lldp(const uint8_t*p,int len){
    int i=0; bool got=false;
    char sysname[40]="", portid[40]="", mgmt[20]="";
    int pvid=-1, vlan_names=0, first_named_vlan=-1;
    while(i+2<=len){
        uint16_t hdr=(p[i]<<8)|p[i+1];
        int type=hdr>>9;
        int tlen=hdr&0x1FF;
        i+=2;
        if(type==0) break;            // end TLV
        if(i+tlen>len) break;
        const uint8_t*v=&p[i];
        if(type==5){ str_copy(sysname,sizeof(sysname),v,tlen); got=true; }       // system name
        else if(type==2){ // port id: skip 1 subtype byte
            if(tlen>1){ str_copy(portid,sizeof(portid),v+1,tlen-1); }
            got=true;
        }
        else if(type==8){ // mgmt address
            if(tlen>=6 && v[1]==1)
                snprintf(mgmt,sizeof(mgmt),"%d.%d.%d.%d",v[2],v[3],v[4],v[5]);
        }
        else if(type==127){ // org-specific
            if(tlen>=4 && v[0]==0x00 && v[1]==0x80 && v[2]==0xC2){
                uint8_t sub=v[3];
                if(sub==1 && tlen>=6){
                    pvid=(v[4]<<8)|v[5];
                } else if(sub==3 && tlen>=7){
                    int vid=(v[4]<<8)|v[5];
                    vlan_names++;
                    if(first_named_vlan<0) first_named_vlan=vid;
                }
            }
        }
        i+=tlen;
    }
    if(got){
        strcpy(sw_proto,"LLDP");
        if(sysname[0]) strncpy(sw_name,sysname,sizeof(sw_name)-1);
        if(portid[0]) strncpy(sw_port,portid,sizeof(sw_port)-1);
        if(mgmt[0]) strncpy(sw_mgmt,mgmt,sizeof(sw_mgmt)-1);
        int show_vlan = (pvid>=0)?pvid:first_named_vlan;
        if(show_vlan>=0) snprintf(sw_vlan,sizeof(sw_vlan),"%d",show_vlan);
        if(vlan_names>1){
            strcpy(sw_vmode,"Trunk (tagged)");
        } else if(pvid>=0){
            strcpy(sw_vmode,"Access (untagged)");
        } else if(first_named_vlan>=0){
            strcpy(sw_vmode,"Tagged");
        }
        sw_vlan_count = vlan_names;
        sw_found=true;
        ESP_LOGI("node","LLDP name=%s port=%s pvid=%d vlanTLVs=%d firstVID=%d",
                 sysname,portid,pvid,vlan_names,first_named_vlan);
    }
    return got;
}

// Parse CDP/FDP.
static bool parse_cdp(const uint8_t*p,int len,const char*proto){
    if(len<4) return false;
    int i=4; bool got=false;
    char devid[40]="", portid[40]="";
    int vlan=-1; char mgmt[20]="";
    while(i+4<=len){
        uint16_t type=(p[i]<<8)|p[i+1];
        uint16_t tlen=(p[i+2]<<8)|p[i+3];
        if(tlen<4 || i+tlen>len) break;
        const uint8_t*v=&p[i+4]; int vl=tlen-4;
        if(type==0x0001){ str_copy(devid,sizeof(devid),v,vl); got=true; }
        else if(type==0x0003){ str_copy(portid,sizeof(portid),v,vl); got=true; }
        else if(type==0x000A && vl>=2){ vlan=(v[0]<<8)|v[1]; }
        else if(type==0x0002 && vl>=9){
            if(vl>=13) snprintf(mgmt,sizeof(mgmt),"%d.%d.%d.%d",v[9],v[10],v[11],v[12]);
        }
        i+=tlen;
    }
    if(got){
        strncpy(sw_proto,proto,sizeof(sw_proto)-1);
        if(devid[0]) strncpy(sw_name,devid,sizeof(sw_name)-1);
        if(portid[0]) strncpy(sw_port,portid,sizeof(sw_port)-1);
        if(mgmt[0]) strncpy(sw_mgmt,mgmt,sizeof(sw_mgmt)-1);
        if(vlan>=0){
            snprintf(sw_vlan,sizeof(sw_vlan),"%d",vlan);
            strcpy(sw_vmode,"Native VLAN");
        }
        sw_found=true;
    }
    return got;
}

// Called for every incoming L2 frame. Filter discovery, else pass to IP stack.
static esp_err_t eth_input_hook(esp_eth_handle_t eth, uint8_t *buf, uint32_t len, void *priv){
    if(len>=14){
        uint16_t et=(buf[12]<<8)|buf[13];
        if(et==0x88CC){
            parse_lldp(buf+14, len-14);
        } else if(len>=22 && et<=1500){
            if(buf[14]==0xAA && buf[15]==0xAA && buf[16]==0x03){
                uint32_t oui=(buf[17]<<16)|(buf[18]<<8)|buf[19];
                uint16_t pid=(buf[20]<<8)|buf[21];
                const uint8_t*pl=buf+22; int pll=len-22;
                if(oui==0x00000C && pid==0x2000) parse_cdp(pl,pll,"CDP");
                else if(oui==0x00E052 && pid==0x2000) parse_cdp(pl,pll,"FDP");
            }
        }
    }
    return esp_netif_receive(eth_netif, buf, len, priv);
}

static void eth_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data){
    (void)arg;(void)base;(void)data;
    if(id==ETHERNET_EVENT_CONNECTED){ net_link=true; ESP_LOGI(TAG,"eth link up"); }
    else if(id==ETHERNET_EVENT_DISCONNECTED){ net_link=false; net_gotip=false; ESP_LOGI(TAG,"eth link down"); }
}
static void got_ip_handler(void *arg, esp_event_base_t base, int32_t id, void *data){
    (void)arg;(void)base;(void)id;
    ip_event_got_ip_t *e=(ip_event_got_ip_t*)data;
    const esp_netif_ip_info_t *ip=&e->ip_info;
    snprintf(net_ip,sizeof(net_ip),IPSTR,IP2STR(&ip->ip));
    snprintf(net_mask,sizeof(net_mask),IPSTR,IP2STR(&ip->netmask));
    snprintf(net_gw,sizeof(net_gw),IPSTR,IP2STR(&ip->gw));
    esp_netif_dns_info_t dns;
    if(esp_netif_get_dns_info(eth_netif,ESP_NETIF_DNS_MAIN,&dns)==ESP_OK)
        snprintf(net_dns,sizeof(net_dns),IPSTR,IP2STR(&dns.ip.u_addr.ip4));
    net_gotip=true;
    ESP_LOGI(TAG,"eth IP %s",net_ip);
}
static void eth_start(void){
    gpio_install_isr_service(0);
    gpio_config_t rst={ .mode=GPIO_MODE_OUTPUT, .pin_bit_mask=1ULL<<ETH_RST }; gpio_config(&rst);
    gpio_set_level(ETH_RST,0); vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(ETH_RST,1); vTaskDelay(pdMS_TO_TICKS(50));

    spi_bus_config_t b={ .mosi_io_num=ETH_MOSI, .miso_io_num=ETH_MISO, .sclk_io_num=ETH_CLK, .quadwp_io_num=-1, .quadhd_io_num=-1 };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&b,SPI_DMA_CH_AUTO));
    spi_device_interface_config_t d={ .mode=0, .clock_speed_hz=12*1000*1000, .spics_io_num=ETH_CS, .queue_size=20 };
    eth_w5500_config_t w5500=ETH_W5500_DEFAULT_CONFIG(SPI2_HOST,&d);
    w5500.int_gpio_num=ETH_INT;
    eth_mac_config_t mac_cfg=ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac=esp_eth_mac_new_w5500(&w5500,&mac_cfg);
    eth_phy_config_t phy_cfg=ETH_PHY_DEFAULT_CONFIG(); phy_cfg.reset_gpio_num=-1;
    esp_eth_phy_t *phy=esp_eth_phy_new_w5500(&phy_cfg);
    esp_eth_config_t eth_cfg=ETH_DEFAULT_CONFIG(mac,phy);
    esp_eth_handle_t eth=NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg,&eth));
    g_eth=eth;
    bool promisc=true;
    esp_eth_ioctl(eth, ETH_CMD_S_PROMISCUOUS, &promisc);
    uint8_t macaddr[6]; esp_read_mac(macaddr,ESP_MAC_ETH);
    esp_eth_ioctl(eth,ETH_CMD_S_MAC_ADDR,macaddr);
    snprintf(net_mac,sizeof(net_mac),"%02X:%02X:%02X:%02X:%02X:%02X",
        macaddr[0],macaddr[1],macaddr[2],macaddr[3],macaddr[4],macaddr[5]);

    esp_netif_config_t ncfg=ESP_NETIF_DEFAULT_ETH();
    eth_netif=esp_netif_new(&ncfg);
    esp_netif_attach(eth_netif,esp_eth_new_netif_glue(eth));
    esp_eth_update_input_path(eth, eth_input_hook, NULL);
    esp_event_handler_register(ETH_EVENT,ESP_EVENT_ANY_ID,eth_event_handler,NULL);
    esp_event_handler_register(IP_EVENT,IP_EVENT_ETH_GOT_IP,got_ip_handler,NULL);
    esp_eth_start(eth);
}

// ================= WIFI =================
static const char* auth_str(wifi_auth_mode_t m){
    switch(m){
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        default: return "?";
    }
}
static void wifi_update_info(void){
    wifi_ap_record_t ap;
    if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK){
        strncpy(wifi_ssid,(char*)ap.ssid,sizeof(wifi_ssid)-1);
        snprintf(wifi_bssid,sizeof(wifi_bssid),"%02X:%02X:%02X:%02X:%02X:%02X",
            ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);
        wifi_rssi=ap.rssi;
        wifi_chan=ap.primary;
        strncpy(wifi_auth,auth_str(ap.authmode),sizeof(wifi_auth)-1);
    }
}
static void wifi_event_handler(void *arg,esp_event_base_t base,int32_t id,void *data){
    (void)arg;(void)data;
    if(base==WIFI_EVENT){
        if(id==WIFI_EVENT_STA_START){ esp_wifi_connect(); }
        else if(id==WIFI_EVENT_STA_DISCONNECTED){
            wifi_conn=false;
            esp_wifi_connect(); // retry
        }
    }
}
static void wifi_got_ip(void *arg,esp_event_base_t base,int32_t id,void *data){
    (void)arg;(void)base;(void)id;
    ip_event_got_ip_t *e=(ip_event_got_ip_t*)data;
    snprintf(wifi_ip,sizeof(wifi_ip),IPSTR,IP2STR(&e->ip_info.ip));
    wifi_conn=true;
    wifi_update_info();
    ESP_LOGI(TAG,"wifi IP %s",wifi_ip);
}
static void wifi_start(void){
    wifi_netif=esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event_handler,NULL);
    esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_got_ip,NULL);
    wifi_config_t wc={0};
    strncpy((char*)wc.sta.ssid,WIFI_SSID,sizeof(wc.sta.ssid)-1);
    strncpy((char*)wc.sta.password,WIFI_PASS,sizeof(wc.sta.password)-1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG,"wifi started, connecting to %s",WIFI_SSID);
}


// ===== history persistence (NVS) =====
static void hist_save(void){
    nvs_handle_t h;
    if(nvs_open("hist",NVS_READWRITE,&h)!=ESP_OK) return;
    nvs_set_i32(h,"head",hist_head);
    nvs_set_i32(h,"count",hist_count);
    nvs_set_blob(h,"data",hist,sizeof(hist));
    nvs_commit(h);
    nvs_close(h);
}
static void hist_load(void){
    nvs_handle_t h;
    if(nvs_open("hist",NVS_READONLY,&h)!=ESP_OK) return;
    int32_t v;
    if(nvs_get_i32(h,"head",&v)==ESP_OK) hist_head=v;
    if(nvs_get_i32(h,"count",&v)==ESP_OK) hist_count=v;
    size_t sz=sizeof(hist);
    nvs_get_blob(h,"data",hist,&sz);
    nvs_close(h);
}
// add one sample (US units already)
static void hist_add(float tF,float hum,float pInHg,int aqi){
    hist[hist_head].t=(int16_t)(tF*10);
    hist[hist_head].h=(int16_t)hum;
    hist[hist_head].p=(int16_t)(pInHg*100);
    hist[hist_head].a=(int16_t)aqi;
    hist_head=(hist_head+1)%HIST_SAMPLES;
    if(hist_count<HIST_SAMPLES) hist_count++;
    hist_save();
}

// ===== trend / prediction =====
// returns slope (units per sample) via simple linear regression on a window
static float slope(float*arr,int n,int idx){
    if(n<3) return 0;
    // reconstruct chronological order from ring
    float sx=0,sy=0,sxy=0,sxx=0;
    for(int i=0;i<n;i++){
        int j=(idx-n+i+TREND_WIN)%TREND_WIN;
        float x=i, y=arr[j];
        sx+=x; sy+=y; sxy+=x*y; sxx+=x*x;
    }
    float d=n*sxx-sx*sx;
    if(d==0) return 0;
    return (n*sxy-sx*sy)/d;   // per-sample slope
}
static void trend_push(float tF,float hum,float pInHg){
    trend_temp[trend_idx]=tF; trend_hum[trend_idx]=hum; trend_pres[trend_idx]=pInHg;
    trend_idx=(trend_idx+1)%TREND_WIN;
    if(trend_n<TREND_WIN) trend_n++;

    predict_msg[0]=0;
    if(trend_n<TREND_WIN) return; // need full window
    // slope per sample; samples ~2s apart -> per-minute = slope*30
    float dT=slope(trend_temp,trend_n,trend_idx)*30.0f;   // F/min
    float dH=slope(trend_hum,trend_n,trend_idx)*30.0f;    // %/min
    float dP=slope(trend_pres,trend_n,trend_idx)*30.0f;   // inHg/min

    float curT=trend_temp[(trend_idx-1+TREND_WIN)%TREND_WIN];
    float curH=trend_hum[(trend_idx-1+TREND_WIN)%TREND_WIN];

    // temp thresholds in F: high=86F(30C), low=41F(5C)
    if(dT>0.3f && curT<86){
        float mins=(86-curT)/dT;
        if(mins<30) snprintf(predict_msg,sizeof(predict_msg),"Temp rising: ~%dmin to HIGH",(int)mins);
    } else if(dT<-0.3f && curT>41){
        float mins=(curT-41)/(-dT);
        if(mins<30) snprintf(predict_msg,sizeof(predict_msg),"Temp falling: ~%dmin to LOW",(int)mins);
    }
    if(!predict_msg[0] && dH>0.8f && curH<70){
        float mins=(70-curH)/dH;
        if(mins<30) snprintf(predict_msg,sizeof(predict_msg),"Humidity rising: ~%dmin to HIGH",(int)mins);
    }
    if(!predict_msg[0] && dP<-0.02f){
        snprintf(predict_msg,sizeof(predict_msg),"Pressure dropping fast - weather change likely");
    }
}

// ================= UI =================
typedef struct {
    lv_obj_t *arc,*val,*warn,*detail;
    float vmin,vmax; float lo_alert,hi_alert; uint32_t color;
    float seen_min, seen_max; bool seen;
} page_t;
static page_t pg[4];

// ---- HOME page: brand, time, weather, location ----
static void home_page(lv_obj_t*tv,int col){
    lv_obj_t *t=lv_tileview_add_tile(tv,col,0,LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(t,lv_color_hex(0x0A0A0B),0);
    lv_obj_t *w1=lv_label_create(t);
    lv_label_set_text(w1,"SENTRY");
    lv_obj_set_style_text_color(w1,lv_color_hex(0xE6C478),0);
    lv_obj_set_style_text_font(w1,&lv_font_montserrat_28,0);
    lv_obj_align(w1,LV_ALIGN_TOP_MID,0,40);
    lv_obj_t *w2=lv_label_create(t);
    lv_label_set_text(w2,NODE_ID);
    lv_obj_set_style_text_color(w2,lv_color_hex(0xC8A24B),0);
    lv_obj_set_style_text_font(w2,&lv_font_montserrat_14,0);
    lv_obj_align(w2,LV_ALIGN_TOP_MID,0,74);
    home_clock=lv_label_create(t);
    lv_obj_set_style_text_color(home_clock,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_style_text_font(home_clock,&lv_font_montserrat_48,0);
    lv_label_set_text(home_clock,"-- : --");
    lv_obj_align(home_clock,LV_ALIGN_CENTER,0,-6);
    wx_icon=lv_obj_create(t);
    lv_obj_set_style_border_width(wx_icon,0,0);
    lv_obj_set_style_pad_all(wx_icon,0,0);
    lv_obj_set_style_bg_opa(wx_icon,LV_OPA_TRANSP,0);
    lv_obj_set_size(wx_icon,26,26);
    lv_obj_clear_flag(wx_icon,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(wx_icon,LV_ALIGN_CENTER,-34,52);
    wx_temp_lbl=lv_label_create(t);
    lv_obj_set_style_text_color(wx_temp_lbl,lv_color_hex(0xE8E9EC),0);
    lv_obj_set_style_text_font(wx_temp_lbl,&lv_font_montserrat_28,0);
    lv_label_set_text(wx_temp_lbl,"--");
    lv_obj_align(wx_temp_lbl,LV_ALIGN_CENTER,18,52);
    wx_cond_lbl=lv_label_create(t);
    lv_obj_set_style_text_color(wx_cond_lbl,lv_color_hex(0x8A8F98),0);
    lv_obj_set_style_text_font(wx_cond_lbl,&lv_font_montserrat_14,0);
    lv_label_set_text(wx_cond_lbl,"");
    lv_obj_align(wx_cond_lbl,LV_ALIGN_CENTER,0,82);
}

static void gauge_page(lv_obj_t*tv,int col,lv_dir_t dir,const char*title,const char*unit,
                       float vmin,float vmax,uint32_t color,page_t*P){
    lv_obj_t *t=lv_tileview_add_tile(tv,col,0,dir);
    lv_obj_set_style_bg_color(t,lv_color_hex(0x0B0F14),0);
    lv_obj_t *arc=lv_arc_create(t);
    lv_obj_set_size(arc,220,220);
    lv_arc_set_rotation(arc,135); lv_arc_set_bg_angles(arc,0,270);
    lv_arc_set_range(arc,(int)vmin,(int)vmax); lv_arc_set_value(arc,(int)vmin);
    lv_obj_set_style_arc_width(arc,12,LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc,12,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc,lv_color_hex(0x222A33),LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc,lv_color_hex(color),LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc,true,LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc,true,LV_PART_MAIN);
    lv_obj_set_style_shadow_color(arc,lv_color_hex(color),LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(arc,18,LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(arc,LV_OPA_60,LV_PART_INDICATOR);
    lv_obj_remove_style(arc,NULL,LV_PART_KNOB);
    lv_obj_clear_flag(arc,LV_OBJ_FLAG_CLICKABLE); lv_obj_center(arc);
    lv_obj_t *ti=lv_label_create(t); lv_label_set_text(ti,title);
    lv_obj_set_style_text_color(ti,lv_color_hex(color),0);
    lv_obj_set_style_text_font(ti,&lv_font_montserrat_20,0);
    lv_obj_align(ti,LV_ALIGN_CENTER,0,-62);
    lv_obj_t *v=lv_label_create(t); lv_label_set_text(v,"--");
    lv_obj_set_style_text_color(v,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_style_text_font(v,&lv_font_montserrat_28,0);
    lv_obj_align(v,LV_ALIGN_CENTER,0,-30);
    lv_obj_t *u=lv_label_create(t); lv_label_set_text(u,unit);
    lv_obj_set_style_text_color(u,lv_color_hex(0x8A97A6),0);
    lv_obj_set_style_text_font(u,&lv_font_montserrat_14,0);
    lv_obj_align(u,LV_ALIGN_CENTER,0,-6);
    lv_obj_t *w=lv_label_create(t); lv_label_set_text(w,"");
    lv_obj_set_style_text_color(w,lv_color_hex(0xFF3030),0);
    lv_obj_set_style_text_font(w,&lv_font_montserrat_14,0);
    lv_obj_align(w,LV_ALIGN_CENTER,0,16);
    lv_obj_t *dl=lv_label_create(t);
    lv_obj_set_style_text_color(dl,lv_color_hex(0x8A97A6),0);
    lv_obj_set_style_text_font(dl,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_align(dl,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_text(dl,"");
    lv_obj_align(dl,LV_ALIGN_CENTER,0,42);
    P->arc=arc; P->val=v; P->warn=w; P->detail=dl;
    P->vmin=vmin; P->vmax=vmax; P->color=color;
    P->seen=false; P->seen_min=0; P->seen_max=0;
}

static void arc_anim_cb(void*obj,int32_t v){ lv_arc_set_value((lv_obj_t*)obj,v); }
static void set_arc_animated(lv_obj_t*arc,int target){
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a,arc);
    lv_anim_set_exec_cb(&a,arc_anim_cb);
    lv_anim_set_values(&a, lv_arc_get_value(arc), target);
    lv_anim_set_time(&a,600);
    lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
    lv_anim_start(&a);
}
static void update_gauge(page_t*P,float value,int dec,const char*hi,const char*lo,const char*status){
    char b[16]; snprintf(b,sizeof(b),"%.*f",dec,value); lv_label_set_text(P->val,b);
    int cv=(int)value;
    if(cv<(int)P->vmin){ cv=(int)P->vmin; }
    if(cv>(int)P->vmax){ cv=(int)P->vmax; }
    set_arc_animated(P->arc,cv);
    bool alert=false;
    if(hi && value>P->hi_alert){ lv_label_set_text(P->warn,hi); alert=true; }
    else if(lo && value<P->lo_alert){ lv_label_set_text(P->warn,lo); alert=true; }
    else lv_label_set_text(P->warn,"");
    lv_obj_set_style_arc_color(P->arc,lv_color_hex(alert?0xFF3030:P->color),LV_PART_INDICATOR);
    if(!P->seen){ P->seen=true; P->seen_min=value; P->seen_max=value; }
    if(value<P->seen_min) P->seen_min=value;
    if(value>P->seen_max) P->seen_max=value;
    if(P->detail){
        char d[64];
        snprintf(d,sizeof(d),"%s\nmin %.*f  max %.*f",
            status?status:"", dec,P->seen_min, dec,P->seen_max);
        lv_label_set_text(P->detail,d);
    }
}

// network info page (text rows)
static lv_obj_t *net_label;
static void net_page(lv_obj_t*tv,int col){
    lv_obj_t *t=lv_tileview_add_tile(tv,col,0,LV_DIR_LEFT|LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(t,lv_color_hex(0x0B0F14),0);
    lv_obj_t *ti=lv_label_create(t); lv_label_set_text(ti,"DEVICE STATS");
    lv_obj_set_style_text_color(ti,lv_color_hex(0x33E0FF),0);
    lv_obj_set_style_text_font(ti,&lv_font_montserrat_20,0);
    lv_obj_align(ti,LV_ALIGN_TOP_MID,0,18);
    net_label=lv_label_create(t);
    lv_obj_set_style_text_color(net_label,lv_color_hex(0xD8E0E8),0);
    lv_obj_set_style_text_font(net_label,&lv_font_montserrat_14,0);
    lv_label_set_text(net_label,"link: ...");
    lv_obj_set_width(net_label,180);
    lv_obj_align(net_label,LV_ALIGN_CENTER,0,8);
    lv_obj_set_style_text_align(net_label,LV_TEXT_ALIGN_CENTER,0);
}
static void update_net(void){
    char buf[256];
    snprintf(buf,sizeof(buf),
        "Link: %s\nIP: %s\nMask: %s\nGW: %s\nDNS: %s\nMAC:\n%s\nDHCP: yes",
        net_link?(net_gotip?"UP":"UP (no IP)"):"DOWN",
        net_ip, net_mask, net_gw, net_dns, net_mac);
    lv_label_set_text(net_label,buf);
}

// ---- WIFI page ----
static lv_obj_t *wifi_label;
static void wifi_page(lv_obj_t*tv,int col){
    lv_obj_t *t=lv_tileview_add_tile(tv,col,0,LV_DIR_LEFT|LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(t,lv_color_hex(0x0B0F14),0);
    lv_obj_t *ti=lv_label_create(t); lv_label_set_text(ti,"WIFI / AP");
    lv_obj_set_style_text_color(ti,lv_color_hex(0x7FD9A0),0);
    lv_obj_set_style_text_font(ti,&lv_font_montserrat_20,0);
    lv_obj_align(ti,LV_ALIGN_TOP_MID,0,18);
    wifi_label=lv_label_create(t);
    lv_obj_set_style_text_color(wifi_label,lv_color_hex(0xD8E0E8),0);
    lv_obj_set_style_text_font(wifi_label,&lv_font_montserrat_14,0);
    lv_label_set_text(wifi_label,"Connecting...");
    lv_obj_set_width(wifi_label,190);
    lv_obj_align(wifi_label,LV_ALIGN_CENTER,0,8);
    lv_obj_set_style_text_align(wifi_label,LV_TEXT_ALIGN_CENTER,0);
}
static const char* rssi_bars(int rssi){
    if(rssi>=-55) return "Excellent";
    if(rssi>=-67) return "Good";
    if(rssi>=-75) return "Fair";
    if(rssi>=-85) return "Weak";
    return "Very Weak";
}
static void update_wifi(void){
    char buf[256];
    if(wifi_conn){
        wifi_update_info();
        snprintf(buf,sizeof(buf),
            "SSID:\n%s\nSignal: %d dBm\n(%s)\nCh: %d  %s\nIP: %s\nBSSID:\n%s",
            wifi_ssid, wifi_rssi, rssi_bars(wifi_rssi),
            wifi_chan, wifi_auth, wifi_ip, wifi_bssid);
    } else {
        snprintf(buf,sizeof(buf),"Connecting to\nWiFi...\n\nSSID: %s",WIFI_SSID);
    }
    lv_label_set_text(wifi_label,buf);
}

// ---- SWITCH STATISTICS page ----
static lv_obj_t *sw_label;
static void switch_page(lv_obj_t*tv,int col){
    lv_obj_t *t=lv_tileview_add_tile(tv,col,0,LV_DIR_LEFT);
    lv_obj_set_style_bg_color(t,lv_color_hex(0x0B0F14),0);
    lv_obj_t *ti=lv_label_create(t); lv_label_set_text(ti,"SWITCH STATS");
    lv_obj_set_style_text_color(ti,lv_color_hex(0xC8A24B),0);
    lv_obj_set_style_text_font(ti,&lv_font_montserrat_20,0);
    lv_obj_align(ti,LV_ALIGN_TOP_MID,0,18);
    sw_label=lv_label_create(t);
    lv_obj_set_style_text_color(sw_label,lv_color_hex(0xD8E0E8),0);
    lv_obj_set_style_text_font(sw_label,&lv_font_montserrat_14,0);
    lv_label_set_text(sw_label,"Listening...");
    lv_obj_set_width(sw_label,190);
    lv_obj_align(sw_label,LV_ALIGN_CENTER,0,8);
    lv_obj_set_style_text_align(sw_label,LV_TEXT_ALIGN_CENTER,0);
}
static void update_switch(void){
    char buf[256];
    if(sw_found){
        snprintf(buf,sizeof(buf),
            "Proto: %s\nSwitch:\n%s\nPort: %s\nVLAN: %s\nMode: %s\nMgmt: %s",
            sw_proto, sw_name, sw_port, sw_vlan, sw_vmode, sw_mgmt);
    } else {
        snprintf(buf,sizeof(buf),
            "Listening for\nLLDP / CDP / FDP...\n\n(can take ~60s,\nneeds managed switch)");
    }
    lv_label_set_text(sw_label,buf);
}

// ================= WEB SERVER (HTTP + WebSocket) =================
static void build_json(char*out,size_t cap){
    time_t now=time(NULL);
    long up = (long)((esp_timer_get_time()-g_boot_us)/1000000);
    snprintf(out,cap,
        "{\"node\":\"" NODE_ID "\",\"temp\":%.1f,\"hum\":%.0f,\"pres\":%.2f,\"gas\":%d,\"predict\":\"%s\","
        "\"link\":%s,\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\",\"dns\":\"%s\",\"mac\":\"%s\","
        "\"sw_found\":%s,\"sw_proto\":\"%s\",\"sw_name\":\"%s\",\"sw_port\":\"%s\","
        "\"sw_vlan\":\"%s\",\"sw_mode\":\"%s\",\"sw_mgmt\":\"%s\","
        "\"wifi\":%s,\"wssid\":\"%s\",\"wrssi\":%d,\"wchan\":%d,\"wauth\":\"%s\",\"wip\":\"%s\",\"wbssid\":\"%s\","
        "\"epoch\":%ld,\"uptime\":%ld}",
        C2F(s_temp),s_hum,hpa2inHg(s_pres),air_index(s_gas),predict_msg,
        net_link?"true":"false", net_ip,net_mask,net_gw,net_dns,net_mac,
        sw_found?"true":"false", sw_proto,sw_name,sw_port,sw_vlan,sw_vmode,sw_mgmt,
        wifi_conn?"true":"false", wifi_ssid,wifi_rssi,wifi_chan,wifi_auth,wifi_ip,wifi_bssid,
        (long)now, up);
}
static esp_err_t root_handler(httpd_req_t*req){
    httpd_resp_set_type(req,"text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t ws_handler(httpd_req_t*req){
    if(req->method==HTTP_GET){
        g_ws_fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG,"ws client connected fd=%d",g_ws_fd);
        return ESP_OK;
    }
    httpd_ws_frame_t f; memset(&f,0,sizeof(f));
    f.type=HTTPD_WS_TYPE_TEXT;
    httpd_ws_recv_frame(req,&f,0);
    return ESP_OK;
}
static void ws_push(void){
    if(!g_httpd || g_ws_fd<0) return;
    static char json[700];
    build_json(json,sizeof(json));
    httpd_ws_frame_t f; memset(&f,0,sizeof(f));
    f.type=HTTPD_WS_TYPE_TEXT;
    f.payload=(uint8_t*)json;
    f.len=strlen(json);
    if(httpd_ws_send_frame_async(g_httpd, g_ws_fd, &f)!=ESP_OK){
        g_ws_fd=-1;
    }
}
static void ws_timer_cb(void*a){ (void)a; ws_push(); }

// serve history as JSON arrays (downsampled 15-min, up to 30 days)
static esp_err_t history_handler(httpd_req_t*req){
    httpd_resp_set_type(req,"application/json");
    // stream in chunks to avoid a huge buffer
    httpd_resp_sendstr_chunk(req,"{\"t\":[");
    char num[16];
    int n=hist_count;
    int start=(hist_head-hist_count+HIST_SAMPLES)%HIST_SAMPLES;
    for(int i=0;i<n;i++){ int j=(start+i)%HIST_SAMPLES;
        snprintf(num,sizeof(num),"%s%.1f",i?",":"",hist[j].t/10.0f); httpd_resp_sendstr_chunk(req,num); }
    httpd_resp_sendstr_chunk(req,"],\"h\":[");
    for(int i=0;i<n;i++){ int j=(start+i)%HIST_SAMPLES;
        snprintf(num,sizeof(num),"%s%d",i?",":"",hist[j].h); httpd_resp_sendstr_chunk(req,num); }
    httpd_resp_sendstr_chunk(req,"],\"p\":[");
    for(int i=0;i<n;i++){ int j=(start+i)%HIST_SAMPLES;
        snprintf(num,sizeof(num),"%s%.2f",i?",":"",hist[j].p/100.0f); httpd_resp_sendstr_chunk(req,num); }
    httpd_resp_sendstr_chunk(req,"],\"a\":[");
    for(int i=0;i<n;i++){ int j=(start+i)%HIST_SAMPLES;
        snprintf(num,sizeof(num),"%s%d",i?",":"",hist[j].a); httpd_resp_sendstr_chunk(req,num); }
    httpd_resp_sendstr_chunk(req,"],\"interval\":15}");
    httpd_resp_sendstr_chunk(req,NULL);
    return ESP_OK;
}

static void start_webserver(void){
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable=true;
    if(httpd_start(&g_httpd,&cfg)==ESP_OK){
        httpd_uri_t root={.uri="/",.method=HTTP_GET,.handler=root_handler};
        httpd_register_uri_handler(g_httpd,&root);
        httpd_uri_t ws={.uri="/ws",.method=HTTP_GET,.handler=ws_handler,.is_websocket=true};
        httpd_register_uri_handler(g_httpd,&ws);
        httpd_uri_t hu={.uri="/history",.method=HTTP_GET,.handler=history_handler};
        httpd_register_uri_handler(g_httpd,&hu);
        ESP_LOGI(TAG,"web server started");
        const esp_timer_create_args_t ta={.callback=&ws_timer_cb,.name="ws_push"};
        esp_timer_handle_t wt; esp_timer_create(&ta,&wt); esp_timer_start_periodic(wt,500000); // 0.5s
    } else ESP_LOGW(TAG,"web server failed to start");
}
static void start_time_sync(void){
    setenv("TZ","EST5EDT,M3.2.0,M11.1.0",1);
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0,"pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG,"SNTP started");
}

// ===== weather icon =====
static void draw_wx_icon(const char*cond){
    if(!wx_icon) return;
    lv_obj_clean(wx_icon);
    uint32_t sun=0xFFD24A, cloud=0xC7CDD6, rain=0x5BA8E0;
    bool isClear = strstr(cond,"Clear")!=NULL;
    bool isRain  = strstr(cond,"Rain")!=NULL || strstr(cond,"Drizzle")!=NULL;
    bool isStorm = strstr(cond,"Thunder")!=NULL;
    if(isClear){
        lv_obj_t*c=lv_obj_create(wx_icon); lv_obj_remove_style_all(c);
        lv_obj_set_size(c,16,16); lv_obj_set_style_radius(c,8,0);
        lv_obj_set_style_bg_color(c,lv_color_hex(sun),0); lv_obj_set_style_bg_opa(c,LV_OPA_COVER,0);
        lv_obj_center(c);
    } else if(isRain||isStorm){
        lv_obj_t*c=lv_obj_create(wx_icon); lv_obj_remove_style_all(c);
        lv_obj_set_size(c,20,11); lv_obj_set_style_radius(c,6,0);
        lv_obj_set_style_bg_color(c,lv_color_hex(cloud),0); lv_obj_set_style_bg_opa(c,LV_OPA_COVER,0);
        lv_obj_align(c,LV_ALIGN_TOP_MID,0,2);
        for(int i=0;i<3;i++){
            lv_obj_t*d=lv_obj_create(wx_icon); lv_obj_remove_style_all(d);
            lv_obj_set_size(d,2,5); lv_obj_set_style_radius(d,1,0);
            lv_obj_set_style_bg_color(d,lv_color_hex(isStorm?0xFFD24A:rain),0);
            lv_obj_set_style_bg_opa(d,LV_OPA_COVER,0);
            lv_obj_align(d,LV_ALIGN_BOTTOM_MID,(i-1)*6,-1);
        }
    } else {
        lv_obj_t*c=lv_obj_create(wx_icon); lv_obj_remove_style_all(c);
        lv_obj_set_size(c,22,12); lv_obj_set_style_radius(c,6,0);
        lv_obj_set_style_bg_color(c,lv_color_hex(cloud),0); lv_obj_set_style_bg_opa(c,LV_OPA_COVER,0);
        lv_obj_center(c);
    }
}
static void make_clock(void){
    clock_label = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_color(clock_label, lv_color_hex(0xC8A24B), 0);
    lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(clock_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(clock_label, LV_OPA_40, 0);
    lv_obj_set_style_pad_left(clock_label, 8, 0);
    lv_obj_set_style_pad_right(clock_label, 8, 0);
    lv_obj_set_style_pad_top(clock_label, 2, 0);
    lv_obj_set_style_pad_bottom(clock_label, 2, 0);
    lv_obj_set_style_radius(clock_label, 8, 0);
    lv_label_set_text(clock_label, "-- : --");
    lv_obj_align(clock_label, LV_ALIGN_BOTTOM_MID, 0, -8);
}
static void update_clock(void){
    if(!clock_label) return;
    time_t now=time(NULL);
    struct tm tm;
    localtime_r(&now,&tm);
    if(tm.tm_year < (2020-1900)){
        lv_label_set_text(clock_label,"-- : --");
        if(home_clock) lv_label_set_text(home_clock,"-- : --");
        return;
    }
    char buf[16];
    int h=tm.tm_hour, m=tm.tm_min;
    int h12 = (h%12==0)?12:h%12;
    snprintf(buf,sizeof(buf),"%d:%02d %s", h12, m, h<12?"AM":"PM");
    lv_label_set_text(clock_label,buf);
    if(home_clock) lv_label_set_text(home_clock,buf);
}
static void update_weather(void){
    if(!wx_valid || !wx_temp_lbl) return;
    draw_wx_icon(wx_cond);
    char b[12]; snprintf(b,sizeof(b),"%d\u00B0F",wx_temp);
    lv_label_set_text(wx_temp_lbl,b);
    if(wx_cond_lbl) lv_label_set_text(wx_cond_lbl,wx_cond);
}
static bool json_num(const char*js,const char*key,int*out){
    const char*p=strstr(js,key); if(!p) return false;
    p+=strlen(key);
    while(*p && (*p==':'||*p==' '||*p=='"')) p++;
    *out=(int)strtol(p,NULL,10); return true;
}
static bool json_str(const char*js,const char*key,char*out,size_t cap){
    const char*p=strstr(js,key); if(!p) return false;
    p+=strlen(key);
    while(*p && (*p==':'||*p==' ')) p++;
    if(*p!='"') return false;
    p++;
    size_t i=0; while(*p && *p!='"' && i<cap-1) out[i++]=*p++;
    out[i]=0; return true;
}
static char wx_buf[1536];
static int  wx_len=0;
static esp_err_t wx_http_evt(esp_http_client_event_t*e){
    if(e->event_id==HTTP_EVENT_ON_DATA){
        int n=e->data_len; if(wx_len+n<(int)sizeof(wx_buf)-1){
            memcpy(wx_buf+wx_len,e->data,n); wx_len+=n; wx_buf[wx_len]=0;
        }
    }
    return ESP_OK;
}
static void weather_fetch_once(void){
    wx_len=0; wx_buf[0]=0;
    char url[256];
    snprintf(url,sizeof(url),
        "http://api.openweathermap.org/data/2.5/weather?lat=%s&lon=%s&units=imperial&appid=%s",
        OWM_LAT,OWM_LON,OWM_API_KEY);
    esp_http_client_config_t cfg={ .url=url, .event_handler=wx_http_evt, .timeout_ms=8000 };
    esp_http_client_handle_t c=esp_http_client_init(&cfg);
    esp_err_t err=esp_http_client_perform(c);
    int status=esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if(err==ESP_OK && status==200 && wx_len>0){
        int t; char cond[16];
        const char*mp=strstr(wx_buf,"\"main\":{");
        if(mp && json_num(mp,"\"temp\"",&t)) wx_temp=t;
        const char*wp=strstr(wx_buf,"\"weather\":[");
        if(wp && json_str(wp,"\"main\"",cond,sizeof(cond))) strncpy(wx_cond,cond,sizeof(wx_cond)-1);
        wx_valid=true;
        ESP_LOGI(TAG,"weather: %s %dF (status %d)",wx_cond,wx_temp,status);
    } else {
        ESP_LOGW(TAG,"weather fetch failed err=%d status=%d len=%d",err,status,wx_len);
    }
}
static void weather_task(void*a){
    (void)a;
    vTaskDelay(pdMS_TO_TICKS(8000));
    while(1){
        weather_fetch_once();
        vTaskDelay(pdMS_TO_TICKS(10*60*1000));
    }
}

// ---- status text helpers ----
static const char* temp_status(float c){
    if(c<5)  return "Cold";
    if(c<18) return "Cool";
    if(c<25) return "Comfortable";
    if(c<30) return "Warm";
    return "Hot";
}
static const char* hum_status(float h){
    if(h<20) return "Very Dry";
    if(h<30) return "Dry";
    if(h<60) return "Comfortable";
    if(h<70) return "Humid";
    return "Very Humid";
}
static const char* pres_status(float hpa){
    if(hpa<1000) return "Low / Stormy";
    if(hpa<1013) return "Slightly Low";
    if(hpa<1023) return "Normal";
    return "High / Clear";
}
static int air_index(float kohm){
    float lo=5.0f, hi=150.0f;
    float idx=(kohm-lo)/(hi-lo)*100.0f;
    if(idx<0){ idx=0; }
    if(idx>100){ idx=100; }
    return (int)idx;
}
static const char* air_status(int idx){
    if(idx>=80) return "Excellent";
    if(idx>=60) return "Good";
    if(idx>=40) return "Fair";
    if(idx>=20) return "Poor";
    return "Very Poor";
}

// ================= MQTT (report to hub) =================
static esp_mqtt_client_handle_t mqtt_client = NULL;
static volatile bool mqtt_conn = false;

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t id, void *data){
    (void)args; (void)base; (void)data;
    if(id == MQTT_EVENT_CONNECTED){
        mqtt_conn = true;
        ESP_LOGI(TAG, "MQTT connected to hub");
        esp_mqtt_client_publish(mqtt_client, MQTT_STATUS, "online", 0, 1, 1);
    } else if(id == MQTT_EVENT_DISCONNECTED){
        mqtt_conn = false;
        ESP_LOGW(TAG, "MQTT disconnected - will retry");
    }
}
static void mqtt_start(void){
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER,
        .session.keepalive = 15,
        .session.last_will = { .topic = MQTT_STATUS, .msg = "offline",
                               .msg_len = 7, .qos = 1, .retain = 1 },
        .network.reconnect_timeout_ms = 3000,
    };
    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
    ESP_LOGI(TAG, "MQTT client started -> %s", MQTT_BROKER);
}
static void mqtt_publish(void){
    if(!mqtt_conn || !mqtt_client) return;
    static char json[800];
    build_json(json, sizeof(json));
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, json, 0, 0, 0);
}
static void mqtt_timer_cb(void *a){ (void)a; mqtt_publish(); }

void app_main(void){
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG,"Sensor Node v3 (wifi)");
    esp_log_level_set("i2c.master",ESP_LOG_NONE);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // NVS is required by WiFi
    esp_err_t nvs = nvs_flash_init();
    if(nvs==ESP_ERR_NVS_NO_FREE_PAGES || nvs==ESP_ERR_NVS_NEW_VERSION_FOUND){
        nvs_flash_erase();
        nvs_flash_init();
    }
    hist_load();  // restore 30-day history from flash

    // display
    gpio_config_t io={ .mode=GPIO_MODE_OUTPUT, .pin_bit_mask=(1ULL<<PIN_CS)|(1ULL<<PIN_DC)|(1ULL<<PIN_RST)|(1ULL<<PIN_BL) };
    gpio_config(&io); gpio_set_level(PIN_BL,1); cs(1);
    spi_bus_config_t bus={ .sclk_io_num=PIN_CLK, .mosi_io_num=PIN_MOSI, .miso_io_num=-1, .quadwp_io_num=-1, .quadhd_io_num=-1, .max_transfer_sz=H_RES*BUF_LINES*2+64 };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev={ .clock_speed_hz=40*1000*1000, .mode=0, .spics_io_num=-1, .queue_size=7 };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST,&dev,&spi));
    panel_reset(); init_reg();

    // LVGL
    lv_init();
    const esp_timer_create_args_t targs={ .callback=&lv_tick_task, .name="lv_tick" };
    esp_timer_handle_t th; esp_timer_create(&targs,&th); esp_timer_start_periodic(th,1000);
    lv_disp_draw_buf_init(&draw_buf,buf1,buf2,H_RES*BUF_LINES);
    static lv_disp_drv_t ddrv; lv_disp_drv_init(&ddrv);
    ddrv.hor_res=H_RES; ddrv.ver_res=V_RES; ddrv.flush_cb=flush_cb; ddrv.draw_buf=&draw_buf;
    lv_disp_drv_register(&ddrv);

    // touch
    gpio_config_t tprst={ .mode=GPIO_MODE_OUTPUT, .pin_bit_mask=1ULL<<TP_RST }; gpio_config(&tprst);
    i2c_master_bus_config_t tb={ .clk_source=I2C_CLK_SRC_DEFAULT, .i2c_port=I2C_NUM_0, .scl_io_num=TP_SCL, .sda_io_num=TP_SDA, .glitch_ignore_cnt=7, .flags.enable_internal_pullup=true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&tb,&tp_bus));
    i2c_device_config_t td={ .dev_addr_length=I2C_ADDR_BIT_LEN_7, .device_address=CST816S_ADDR, .scl_speed_hz=400000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(tp_bus,&td,&tp_dev));
    tp_reset();
    static lv_indev_drv_t idrv; lv_indev_drv_init(&idrv);
    idrv.type=LV_INDEV_TYPE_POINTER; idrv.read_cb=touch_cb; lv_indev_drv_register(&idrv);

    // sensor
    i2c_master_bus_config_t sb={ .clk_source=I2C_CLK_SRC_DEFAULT, .i2c_port=I2C_NUM_1, .scl_io_num=BME_SCL, .sda_io_num=BME_SDA, .glitch_ignore_cnt=7, .flags.enable_internal_pullup=true };
    ESP_ERROR_CHECK(i2c_new_master_bus(&sb,&bme_bus));
    i2c_device_config_t sd={ .dev_addr_length=I2C_ADDR_BIT_LEN_7, .device_address=BME_ADDR, .scl_speed_hz=100000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bme_bus,&sd,&bme_dev));
    memset(&bme,0,sizeof(bme));
    bme.intf=BME68X_I2C_INTF; bme.read=bme_rd; bme.write=bme_wr; bme.delay_us=bme_us; bme.amb_temp=25;
    sensor_ok=(bme68x_init(&bme)==BME68X_OK);
    if(sensor_ok){
        bme_conf.os_hum=BME68X_OS_2X; bme_conf.os_pres=BME68X_OS_4X; bme_conf.os_temp=BME68X_OS_8X;
        bme_conf.filter=BME68X_FILTER_OFF; bme_conf.odr=BME68X_ODR_NONE;
        bme68x_set_conf(&bme_conf,&bme);
        bme_heat.enable=BME68X_ENABLE; bme_heat.heatr_temp=300; bme_heat.heatr_dur=100;
        bme68x_set_heatr_conf(BME68X_FORCED_MODE,&bme_heat,&bme);
    }

    g_boot_us = esp_timer_get_time();
    eth_start();
    wifi_start();
    start_time_sync();
    start_webserver();
    mqtt_start();
    const esp_timer_create_args_t _mqtt_ta = {.callback=&mqtt_timer_cb, .name="mqtt_pub"};
    esp_timer_handle_t _mqtt_t; esp_timer_create(&_mqtt_ta, &_mqtt_t);
    esp_timer_start_periodic(_mqtt_t, 3000000);  // publish every 3s
    xTaskCreatePinnedToCore(weather_task,"weather",6144,NULL,3,NULL,1);
    xTaskCreatePinnedToCore(sensor_task,"sensor",4096,NULL,4,NULL,1);

    lv_obj_t *tv=lv_tileview_create(lv_scr_act());
    lv_obj_set_style_bg_color(tv,lv_color_hex(0x0B0F14),0);
    // smoother swipe: snap crisply and keep the scroll animation short so the
    // panel isn't asked to redraw a long full-screen slide (which causes cutting)
    // shorter scroll-snap animation so the panel redraws fewer full-screen
    // frames per swipe (the long slide is what shows "cuts")
    lv_obj_set_style_anim_time(tv, 120, 0);
    home_page(tv,0);
    gauge_page(tv,1,LV_DIR_LEFT|LV_DIR_RIGHT,"TEMP","\u00B0F", 32,122, 0xFF8C1A,&pg[0]);
    gauge_page(tv,2,LV_DIR_LEFT|LV_DIR_RIGHT,"HUMIDITY","%",0,100,0x00FFFF,&pg[1]);
    gauge_page(tv,3,LV_DIR_LEFT|LV_DIR_RIGHT,"PRESSURE","inHg",28,31,0x33E066,&pg[2]);
    gauge_page(tv,4,LV_DIR_LEFT|LV_DIR_RIGHT,"AIR QUALITY","AQI",0,100,0xFFD000,&pg[3]);
    net_page(tv,5);
    wifi_page(tv,6);
    switch_page(tv,7);
    pg[0].hi_alert=86; pg[0].lo_alert=41;
    pg[1].hi_alert=70; pg[1].lo_alert=20;
    pg[2].hi_alert=99; pg[2].lo_alert=-99;
    pg[3].hi_alert=99999; pg[3].lo_alert=30;

    make_clock();

    int64_t lastui=0;
    while(1){
        lv_timer_handler();
        int64_t now=esp_timer_get_time()/1000;
        if(now-lastui>500){
            lastui=now;
            if(s_valid){
                float tF=C2F(s_temp);
                float pInHg=hpa2inHg(s_pres);
                int aqi=air_index(s_gas);

                // trend + prediction
                trend_push(tF, s_hum, pInHg);

                char tstat[64];
                if(predict_msg[0]) snprintf(tstat,sizeof(tstat),"%s",predict_msg);
                else snprintf(tstat,sizeof(tstat),"%s",temp_status(s_temp));
                update_gauge(&pg[0],tF,1,"HIGH TEMP","LOW TEMP",tstat);
                update_gauge(&pg[1],s_hum,0,"HIGH HUMIDITY","LOW HUMIDITY",hum_status(s_hum));
                update_gauge(&pg[2],pInHg,2,NULL,NULL,pres_status(s_pres));
                update_gauge(&pg[3],(float)aqi,0,NULL,"POOR AIR",air_status(aqi));

                // store history every 15 min
                int64_t nowms=esp_timer_get_time()/1000;
                if(last_hist_ms==0 || nowms-last_hist_ms>=HIST_INTERVAL_MS){
                    last_hist_ms=nowms;
                    hist_add(tF, s_hum, pInHg, aqi);
                }
            }
            update_net();
            update_wifi();
            update_switch();
            update_clock();
            update_weather();
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
