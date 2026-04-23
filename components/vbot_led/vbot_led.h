#pragma once
#include "esp_err.h"

typedef enum {
    LED_FX_OFF      = 0,
    LED_FX_IDLE,       // breathing xanh nhạt
    LED_FX_LISTENING,  // spinner xanh lá
    LED_FX_THINKING,   // pulse vàng
    LED_FX_SPEAKING,   // wave xanh dương
    LED_FX_HAPPY,      // rainbow chase
    LED_FX_ERROR,      // flash đỏ
    LED_FX_WIFI,       // scanning trắng
} led_fx_t;

esp_err_t vbot_led_init(void);
void      vbot_led_set_fx(led_fx_t fx);
void      vbot_led_set_effect(const char *name);  // for HTTP API
void      vbot_led_tick(void);                    // call ~30fps
void      vbot_led_set_brightness(uint8_t bri);
