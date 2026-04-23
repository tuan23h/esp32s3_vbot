#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef enum { WIFI_ST_IDLE, WIFI_ST_CONNECTING, WIFI_ST_CONNECTED, WIFI_ST_AP } wifi_state_t;

esp_err_t    vbot_wifi_init(void);
wifi_state_t vbot_wifi_state(void);
bool         vbot_wifi_connected(void);
void         vbot_wifi_start_ap(void);   // bật AP mode để cấu hình
void         vbot_wifi_stop_ap(void);
