#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "vbot_ws.h"
#include "vbot_config.h"
#include "vbot_audio.h"    // for vbot_speaker_feed, vbot_on_vad_silence etc.

static const char *TAG = "WS";

static esp_websocket_client_handle_t s_ws    = NULL;
static volatile ws_state_t           s_state = WS_ST_DISCONNECTED;

/* Fragment reassembly (server may split a WAV into multiple frames) */
#define FRAG_MAX  (128 * 1024)
static uint8_t *s_frag    = NULL;
static int      s_frag_len = 0;
static bool     s_frag_text = false;

/* Callbacks declared in main.c */
extern void vbot_on_server_json(const char *json);
extern void vbot_on_server_audio(const uint8_t *data, size_t len);

static void handle_json(const char *s)
{
    ESP_LOGD(TAG, "RX JSON: %.120s", s);
    vbot_on_server_json(s);
}

static void handle_audio(const uint8_t *data, size_t len)
{
    ESP_LOGD(TAG, "RX audio %u bytes", (unsigned)len);
    vbot_on_server_audio(data, len);
}

static void ws_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    esp_websocket_event_data_t *ev = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_state = WS_ST_CONNECTED;
        s_frag_len = 0;
        ESP_LOGI(TAG, "Connected ✓");
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        s_state = WS_ST_DISCONNECTED;
        s_frag_len = 0;
        ESP_LOGW(TAG, "Disconnected");
        break;

    case WEBSOCKET_EVENT_DATA: {
        if (!ev->data_ptr || ev->data_len <= 0) break;
        bool is_text   = (ev->op_code == 0x01);
        bool is_binary = (ev->op_code == 0x02);
        bool is_cont   = (ev->op_code == 0x00);

        if (!is_cont) {
            s_frag_text = is_text;
            s_frag_len  = 0;
        }
        int avail = FRAG_MAX - s_frag_len - 1;
        int copy  = ev->data_len < avail ? ev->data_len : avail;
        if (s_frag && copy > 0) {
            memcpy(s_frag + s_frag_len, ev->data_ptr, copy);
            s_frag_len += copy;
        }
        if (!ev->fin) break; /* wait for final fragment */

        if (s_frag_text) {
            s_frag[s_frag_len] = '\0';
            handle_json((char *)s_frag);
        } else {
            handle_audio(s_frag, s_frag_len);
        }
        s_frag_len = 0;
        break;
    }
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "Error");
        s_state = WS_ST_DISCONNECTED;
        break;
    default: break;
    }
}

esp_err_t vbot_ws_init(void)
{
    s_frag = heap_caps_malloc(FRAG_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_frag) return ESP_ERR_NO_MEM;

    /* mic chunk callback → send audio to server */
    vbot_mic_set_chunk_cb([](const int16_t *pcm, size_t n) {
        vbot_ws_send_audio(pcm, n);
    });
    return ESP_OK;
}

esp_err_t vbot_ws_connect(void)
{
    vbot_config_t *c = vbot_config_get();
    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d%s",
             c->server_host, c->server_port, CFG_WS_PATH);

    esp_websocket_client_config_t ws_cfg = {
        .uri                  = uri,
        .buffer_size          = 64 * 1024,
        .out_buffer_size      = 16 * 1024,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms   = 10000,
        .ping_interval_sec    = 20,
    };
    s_ws = esp_websocket_client_init(&ws_cfg);
    if (!s_ws) return ESP_FAIL;

    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY,
                                   ws_event_handler, NULL);
    s_state = WS_ST_CONNECTING;
    return esp_websocket_client_start(s_ws);
}

ws_state_t vbot_ws_state(void)    { return s_state; }
bool       vbot_ws_connected(void){ return s_state == WS_ST_CONNECTED && esp_websocket_client_is_connected(s_ws); }

esp_err_t vbot_ws_send_audio(const int16_t *pcm, size_t samples)
{
    if (!vbot_ws_connected()) return ESP_ERR_INVALID_STATE;
    int r = esp_websocket_client_send_bin(s_ws, (char*)pcm,
                                           samples * 2, pdMS_TO_TICKS(100));
    return r < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t vbot_ws_send_json(const char *json)
{
    if (!vbot_ws_connected()) return ESP_ERR_INVALID_STATE;
    int r = esp_websocket_client_send_text(s_ws, json, strlen(json), pdMS_TO_TICKS(200));
    return r < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t vbot_ws_send_end_speech(void) { return vbot_ws_send_json("{\"type\":\"end_speech\"}"); }
esp_err_t vbot_ws_send_mute(bool m) {
    char buf[48]; snprintf(buf,sizeof(buf),"{\"type\":\"mute\",\"value\":%s}",m?"true":"false");
    return vbot_ws_send_json(buf);
}
esp_err_t vbot_ws_send_volume(bool up) {
    return vbot_ws_send_json(up ? "{\"type\":\"volume_up\"}" : "{\"type\":\"volume_down\"}");
}
