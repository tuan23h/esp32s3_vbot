#pragma once
#include "esp_err.h"

typedef enum { BTN_WAKE=0, BTN_VOL_UP, BTN_VOL_DOWN, BTN_COUNT } btn_id_t;
typedef enum { BTN_EVT_PRESS, BTN_EVT_RELEASE, BTN_EVT_HOLD } btn_evt_t;
typedef void (*btn_cb_t)(btn_id_t id, btn_evt_t evt);

esp_err_t vbot_buttons_init(void);
void      vbot_buttons_set_cb(btn_cb_t cb);
