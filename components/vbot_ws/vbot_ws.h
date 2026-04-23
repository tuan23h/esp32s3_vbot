#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    WS_ST_DISCONNECTED = 0,
    WS_ST_CONNECTING,
    WS_ST_CONNECTED,
} ws_state_t;

esp_err_t  vbot_ws_init(void);
esp_err_t  vbot_ws_connect(void);
ws_state_t vbot_ws_state(void);
bool       vbot_ws_connected(void);

// Send audio chunk (raw PCM int16, 16kHz mono)
esp_err_t vbot_ws_send_audio(const int16_t *pcm, size_t samples);

// Send JSON control
esp_err_t vbot_ws_send_json(const char *json);
esp_err_t vbot_ws_send_end_speech(void);
esp_err_t vbot_ws_send_mute(bool muted);
esp_err_t vbot_ws_send_volume(bool up);
