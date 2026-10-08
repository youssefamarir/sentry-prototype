# SENTRY Sensor Node

Firmware for the SENTRY sensor node, an ESP32-S3 device that sits in a network closet (IDF/MDF), watches the environment, identifies the switch port it is plugged into, and reports to a Raspberry Pi hub over MQTT.

Written in C on ESP-IDF with PlatformIO.

## What it does

- Reads temperature, humidity, pressure and gas resistance from a BME688 over I2C
- Touchscreen UI in LVGL: swipeable pages for each reading, network info, Wi-Fi info and switch info
- Ethernet (W5500 over SPI) and Wi-Fi running at the same time
- Switch discovery by parsing raw LLDP, CDP and FDP frames: switch name, port, VLAN, management IP
- Publishes a JSON payload to the hub over MQTT every 3 seconds, with a retained Last Will so the hub knows when a node drops
- 30 days of history (15-minute samples) kept in NVS
- Trend-based warnings, for example "Temp rising: ~12min to HIGH"
- Local web dashboard served from the node over HTTP + WebSocket
- Clock from SNTP and outside weather from OpenWeatherMap

## Hardware

Waveshare ESP32-S3-ETH, BME688, 1.28" GC9A01 round LCD with CST816S touch.

| Device | Bus | Pins |
|---|---|---|
| W5500 Ethernet | SPI2 | SCLK 13, MISO 12, MOSI 11, CS 14, INT 10, RST 9 |
| GC9A01 display | SPI3 | CLK 36, MOSI 35, CS 34, DC 33, RST 37, BL 21 |
| CST816S touch | I2C0 | SDA 47, SCL 48, INT 38, RST 39 |
| BME688 sensor | I2C1 | SDA 1, SCL 2 |

GPIO 45 and 46 are left unused because they are strapping pins.

## Build and flash

1. Install VS Code with the PlatformIO extension.
2. Copy `src/secrets.example.h` to `src/secrets.h` and fill in your Wi-Fi, MQTT broker address and OpenWeatherMap key.
3. Set `NODE_ID` in `src/main.c`. It must be unique per node (`closet1`, `closet2`, ...).
4. Build and upload:

```
pio run -t upload
pio device monitor
```

## MQTT topics

| Topic | Payload |
|---|---|
| `sentry/<node_id>/data` | JSON with sensor readings, link state, switch info, Wi-Fi info, uptime |
| `sentry/<node_id>/status` | `online` / `offline` (retained, offline is the Last Will) |

## Project layout

```
src/main.c            firmware
src/web_page.h        dashboard page compiled into the firmware
src/web_index.html    source of that page
src/bme68x*           Bosch BME68x sensor API
partitions.csv        3 MB app partition
sdkconfig.defaults    ESP-IDF and LVGL settings
```
