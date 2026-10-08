/* Minimal LVGL v8 config for ESP32-S3 + GC9A01 240x240 */
#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1          /* GC9A01 SPI wants byte-swapped 16-bit */

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (48U * 1024U)

#define LV_DPI_DEF 130
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (esp_timer_get_time() / 1000)

#define LV_USE_LOG 0

/* Fonts we'll use */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/* Widgets we need */
#define LV_USE_ARC 1
#define LV_USE_LABEL 1
#define LV_USE_BAR 1

#endif /* LV_CONF_H */
