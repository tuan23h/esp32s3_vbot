#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "vbot_webserver.h"
#include "vbot_config.h"
#include "vbot_wifi.h"

static const char *TAG  = "WEB";
static httpd_handle_t s_server = NULL;

// ── Embedded HTML (gzip) ──────────────────────────────────────────────────────
// www/index.html được nhúng vào firmware qua CMake embed_files
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// ── Helpers ───────────────────────────────────────────────────────────────────
static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req,  "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, s ? s : "{}");
    free(s);
    cJSON_Delete(root);
    return ESP_OK;
}

static cJSON *parse_body(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 4096) return cJSON_CreateObject();
    char *buf = malloc(len + 1);
    if (!buf) return cJSON_CreateObject();
    httpd_req_recv(req, buf, len);
    buf[len] = '\0';
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    return j ? j : cJSON_CreateObject();
}

static int ji(cJSON *j, const char *k, int def)
{
    cJSON *v = cJSON_GetObjectItem(j, k);
    return (v && cJSON_IsNumber(v)) ? (int)v->valuedouble : def;
}
static bool jb(cJSON *j, const char *k, bool def)
{
    cJSON *v = cJSON_GetObjectItem(j, k);
    if (!v) return def;
    return cJSON_IsTrue(v);
}
static const char *js(cJSON *j, const char *k, const char *def)
{
    cJSON *v = cJSON_GetObjectItem(j, k);
    return (v && cJSON_IsString(v)) ? v->valuestring : def;
}

// ── GET / → index.html ───────────────────────────────────────────────────────
static esp_err_t h_index(httpd_req_t *req)
{
    size_t len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    httpd_resp_send(req, (char *)index_html_start, len);
    return ESP_OK;
}

// ── GET /api/config ───────────────────────────────────────────────────────────
static esp_err_t h_get_config(httpd_req_t *req)
{
    vbot_config_t *c = vbot_config_get();
    cJSON *j = cJSON_CreateObject();

    cJSON_AddStringToObject(j, "wifi_ssid",   c->wifi_ssid);
    cJSON_AddStringToObject(j, "server_host", c->server_host);
    cJSON_AddNumberToObject(j, "server_port", c->server_port);

    cJSON_AddBoolToObject  (j, "mic_enabled", c->mic_enabled);
    cJSON_AddNumberToObject(j, "mic_sck",     c->mic_sck);
    cJSON_AddNumberToObject(j, "mic_ws",      c->mic_ws);
    cJSON_AddNumberToObject(j, "mic_sd",      c->mic_sd);
    cJSON_AddNumberToObject(j, "mic_gain",    c->mic_gain);

    cJSON_AddBoolToObject  (j, "spk_enabled", c->spk_enabled);
    cJSON_AddNumberToObject(j, "spk_bck",     c->spk_bck);
    cJSON_AddNumberToObject(j, "spk_ws",      c->spk_ws);
    cJSON_AddNumberToObject(j, "spk_dout",    c->spk_dout);
    cJSON_AddNumberToObject(j, "spk_volume",  c->spk_volume);

    cJSON_AddNumberToObject(j, "vad_rms_threshold", c->vad_rms_threshold);
    cJSON_AddNumberToObject(j, "vad_silence_ms",    c->vad_silence_ms);
    cJSON_AddNumberToObject(j, "vad_min_speech_ms", c->vad_min_speech_ms);

    cJSON_AddNumberToObject(j, "disp_type",    c->disp_type);
    cJSON_AddNumberToObject(j, "disp_width",   c->disp_width);
    cJSON_AddNumberToObject(j, "disp_height",  c->disp_height);
    cJSON_AddBoolToObject  (j, "disp_rotate",  c->disp_rotate);
    cJSON_AddNumberToObject(j, "disp_spi_clk", c->disp_spi_clk);
    cJSON_AddNumberToObject(j, "disp_spi_mosi",c->disp_spi_mosi);
    cJSON_AddNumberToObject(j, "disp_spi_cs",  c->disp_spi_cs);
    cJSON_AddNumberToObject(j, "disp_dc",      c->disp_dc);
    cJSON_AddNumberToObject(j, "disp_rst",     c->disp_rst);
    cJSON_AddNumberToObject(j, "disp_bl",      c->disp_bl);
    cJSON_AddNumberToObject(j, "disp_i2c_sda", c->disp_i2c_sda);
    cJSON_AddNumberToObject(j, "disp_i2c_scl", c->disp_i2c_scl);
    cJSON_AddNumberToObject(j, "disp_i2c_addr",c->disp_i2c_addr);

    cJSON_AddNumberToObject(j, "btn_wake",     c->btn_wake);
    cJSON_AddNumberToObject(j, "btn_vol_up",   c->btn_vol_up);
    cJSON_AddNumberToObject(j, "btn_vol_down", c->btn_vol_down);
    cJSON_AddBoolToObject  (j, "btn_active_low",c->btn_active_low);

    cJSON_AddNumberToObject(j, "led_gpio",      c->led_gpio);
    cJSON_AddNumberToObject(j, "led_count",     c->led_count);
    cJSON_AddNumberToObject(j, "led_brightness",c->led_brightness);

    cJSON_AddStringToObject(j, "device_name",   c->device_name);

    /* runtime status */
    cJSON_AddBoolToObject(j, "wifi_connected", vbot_wifi_connected());
    cJSON_AddBoolToObject(j, "ap_mode", vbot_wifi_state() == WIFI_ST_AP);

    return send_json(req, j);
}

// ── POST /api/config — partial update ────────────────────────────────────────
static esp_err_t h_post_config(httpd_req_t *req)
{
    cJSON *body = parse_body(req);
    vbot_config_t *c = vbot_config_get();

    #define UPS(field, key)  { const char *v=js(body,key,NULL); if(v) strlcpy(c->field,v,sizeof(c->field)); }
    #define UPN(field, key)  { cJSON *v=cJSON_GetObjectItem(body,key); if(v&&cJSON_IsNumber(v)) c->field=(typeof(c->field))v->valuedouble; }
    #define UPB(field, key)  { cJSON *v=cJSON_GetObjectItem(body,key); if(v) c->field=cJSON_IsTrue(v); }

    UPS(server_host,  "server_host")  UPN(server_port, "server_port")
    UPB(mic_enabled,  "mic_enabled")  UPN(mic_sck,"mic_sck") UPN(mic_ws,"mic_ws") UPN(mic_sd,"mic_sd") UPN(mic_gain,"mic_gain")
    UPB(spk_enabled,  "spk_enabled")  UPN(spk_bck,"spk_bck") UPN(spk_ws,"spk_ws") UPN(spk_dout,"spk_dout") UPN(spk_volume,"spk_volume")
    UPN(vad_rms_threshold,"vad_rms_threshold") UPN(vad_silence_ms,"vad_silence_ms") UPN(vad_min_speech_ms,"vad_min_speech_ms")
    UPN(disp_type,"disp_type") UPN(disp_width,"disp_width") UPN(disp_height,"disp_height") UPB(disp_rotate,"disp_rotate")
    UPN(disp_spi_clk,"disp_spi_clk") UPN(disp_spi_mosi,"disp_spi_mosi") UPN(disp_spi_cs,"disp_spi_cs")
    UPN(disp_dc,"disp_dc") UPN(disp_rst,"disp_rst") UPN(disp_bl,"disp_bl")
    UPN(disp_i2c_sda,"disp_i2c_sda") UPN(disp_i2c_scl,"disp_i2c_scl") UPN(disp_i2c_addr,"disp_i2c_addr")
    UPN(btn_wake,"btn_wake") UPN(btn_vol_up,"btn_vol_up") UPN(btn_vol_down,"btn_vol_down") UPB(btn_active_low,"btn_active_low")
    UPN(led_gpio,"led_gpio") UPN(led_count,"led_count") UPN(led_brightness,"led_brightness")
    UPS(device_name,"device_name")

    cJSON_Delete(body);
    vbot_config_save(c);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

// ── POST /api/wifi ────────────────────────────────────────────────────────────
static esp_err_t h_post_wifi(httpd_req_t *req)
{
    cJSON *body = parse_body(req);
    vbot_config_t *c = vbot_config_get();
    const char *ssid = js(body, "ssid", "");
    const char *pass = js(body, "password", "");
    strlcpy(c->wifi_ssid, ssid, sizeof(c->wifi_ssid));
    strlcpy(c->wifi_pass, pass, sizeof(c->wifi_pass));
    cJSON_Delete(body);
    vbot_config_save(c);
    // Reconnect in background
    vbot_wifi_init();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

// ── GET /api/wifi/scan ────────────────────────────────────────────────────────
static esp_err_t h_wifi_scan(httpd_req_t *req)
{
    wifi_scan_config_t sc = { .scan_type=WIFI_SCAN_TYPE_ACTIVE, .scan_time.active={100,200} };
    esp_wifi_scan_start(&sc, true);
    uint16_t n = 20;
    wifi_ap_record_t aps[20];
    esp_wifi_scan_get_ap_records(&n, aps);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char*)aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(ap, "auth", aps[i].authmode);
        cJSON_AddItemToArray(arr, ap);
    }
    cJSON_AddItemToObject(root, "networks", arr);
    return send_json(req, root);
}

// ── POST /api/wifi/ap ─────────────────────────────────────────────────────────
static esp_err_t h_wifi_ap(httpd_req_t *req)
{
    cJSON *body = parse_body(req);
    bool force = jb(body, "force", false);
    cJSON_Delete(body);
    if (force) vbot_wifi_start_ap();
    else       vbot_wifi_stop_ap();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

// ── POST /api/server/test ─────────────────────────────────────────────────────
static esp_err_t h_server_test(httpd_req_t *req)
{
    // Thử TCP connect tới server
    cJSON *body = parse_body(req);
    const char *host = js(body, "host", "");
    int port = ji(body, "port", 3000);
    cJSON_Delete(body);

    cJSON *r = cJSON_CreateObject();
    // Dùng esp_http_client để test connection
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/status", host, port);
    // Simplified test — just resolve hostname
    struct addrinfo hints = {.ai_family=AF_UNSPEC, .ai_socktype=SOCK_STREAM};
    struct addrinfo *res;
    char port_str[8]; snprintf(port_str, sizeof(port_str), "%d", port);
    bool ok = (getaddrinfo(host, port_str, &hints, &res) == 0);
    if (res) freeaddrinfo(res);

    cJSON_AddBoolToObject(r, "ok", ok);
    if (!ok) cJSON_AddStringToObject(r, "error", "Cannot resolve host");
    return send_json(req, r);
}

// ── GET /api/audio/rms ────────────────────────────────────────────────────────
// Extern từ vbot_audio component
extern float vbot_mic_get_rms(void);

static esp_err_t h_audio_rms(httpd_req_t *req)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "rms", vbot_mic_get_rms());
    return send_json(req, r);
}

// ── POST /api/audio/beep ──────────────────────────────────────────────────────
extern void vbot_speaker_beep(int freq_hz, int duration_ms);

static esp_err_t h_audio_beep(httpd_req_t *req)
{
    vbot_speaker_beep(440, 300);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

// ── POST /api/led/test ────────────────────────────────────────────────────────
extern void vbot_led_set_effect(const char *effect);

static esp_err_t h_led_test(httpd_req_t *req)
{
    cJSON *body = parse_body(req);
    const char *fx = js(body, "effect", "idle");
    vbot_led_set_effect(fx);
    cJSON_Delete(body);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return send_json(req, r);
}

// ── GET /api/system ───────────────────────────────────────────────────────────
static esp_err_t h_system(httpd_req_t *req)
{
    esp_chip_info_t ci; esp_chip_info(&ci);
    uint32_t flash_sz = 0;
    esp_flash_get_size(NULL, &flash_sz);
    size_t psram = esp_psram_get_size();

    wifi_ap_record_t ap = {}; esp_wifi_sta_get_ap_info(&ap);
    esp_netif_ip_info_t ip_info = {};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) esp_netif_get_ip_info(netif, &ip_info);
    char ip_str[16]; snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));

    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "chip",        "ESP32-S3");
    cJSON_AddNumberToObject(r, "flash_mb",    flash_sz / (1024*1024));
    cJSON_AddNumberToObject(r, "psram_mb",    psram / (1024*1024));
    cJSON_AddNumberToObject(r, "free_heap_kb", esp_get_free_heap_size() / 1024);
    cJSON_AddNumberToObject(r, "uptime_s",    (int)(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(r, "version",     "1.0.0");
    cJSON_AddStringToObject(r, "ip",          ip_str);
    return send_json(req, r);
}

// ── POST /api/system/reboot ───────────────────────────────────────────────────
static esp_err_t h_reboot(httpd_req_t *req)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    send_json(req, r);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ── POST /api/system/reset ────────────────────────────────────────────────────
static esp_err_t h_reset(httpd_req_t *req)
{
    vbot_config_reset();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    send_json(req, r);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ── Route registration ────────────────────────────────────────────────────────
#define REG(m, u, h) { httpd_uri_t uri={.uri=u,.method=m,.handler=h}; httpd_register_uri_handler(s_server,&uri); }

esp_err_t vbot_webserver_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 20;
    cfg.stack_size       = 8192;
    cfg.lru_purge_enable = true;

    ESP_ERROR_CHECK(httpd_start(&s_server, &cfg));

    REG(HTTP_GET,  "/",                  h_index)
    REG(HTTP_GET,  "/api/config",        h_get_config)
    REG(HTTP_POST, "/api/config",        h_post_config)
    REG(HTTP_POST, "/api/wifi",          h_post_wifi)
    REG(HTTP_GET,  "/api/wifi/scan",     h_wifi_scan)
    REG(HTTP_POST, "/api/wifi/ap",       h_wifi_ap)
    REG(HTTP_POST, "/api/server/test",   h_server_test)
    REG(HTTP_GET,  "/api/audio/rms",     h_audio_rms)
    REG(HTTP_POST, "/api/audio/beep",    h_audio_beep)
    REG(HTTP_POST, "/api/led/test",      h_led_test)
    REG(HTTP_GET,  "/api/system",        h_system)
    REG(HTTP_POST, "/api/system/reboot", h_reboot)
    REG(HTTP_POST, "/api/system/reset",  h_reset)

    ESP_LOGI(TAG, "HTTP server started on port %d", cfg.server_port);
    return ESP_OK;
}

esp_err_t vbot_webserver_stop(void)
{
    return s_server ? httpd_stop(s_server) : ESP_OK;
}
