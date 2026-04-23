#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "vbot_wifi.h"
#include "vbot_config.h"

static const char *TAG = "WIFI";
static EventGroupHandle_t s_eg;
#define BIT_CONNECTED BIT0
#define BIT_FAIL      BIT1

static volatile wifi_state_t s_state = WIFI_ST_IDLE;
static int s_retry = 0;
#define MAX_RETRY 5

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
            s_state = WIFI_ST_CONNECTING;
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_state = WIFI_ST_IDLE;
            if (s_retry++ < MAX_RETRY) {
                esp_wifi_connect();
                ESP_LOGI(TAG, "Retry %d/%d", s_retry, MAX_RETRY);
            } else {
                xEventGroupSetBits(s_eg, BIT_FAIL);
                ESP_LOGW(TAG, "Connect failed — starting AP");
                vbot_wifi_start_ap();
            }
        } else if (id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *e = data;
            ESP_LOGI(TAG, "AP: client connected "MACSTR, MAC2STR(e->mac));
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        s_state = WIFI_ST_CONNECTED;
        xEventGroupSetBits(s_eg, BIT_CONNECTED);
    }
}

esp_err_t vbot_wifi_init(void)
{
    s_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL));

    vbot_config_t *c = vbot_config_get();
    if (strlen(c->wifi_ssid) == 0) {
        ESP_LOGI(TAG, "No SSID configured — starting AP");
        vbot_wifi_start_ap();
        return ESP_OK;
    }

    wifi_config_t wc = {};
    strlcpy((char*)wc.sta.ssid, c->wifi_ssid, sizeof(wc.sta.ssid));
    strlcpy((char*)wc.sta.password, c->wifi_pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to: %s", c->wifi_ssid);
    xEventGroupWaitBits(s_eg, BIT_CONNECTED | BIT_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    return ESP_OK;
}

void vbot_wifi_start_ap(void)
{
    vbot_config_t *c = vbot_config_get();
    wifi_config_t ap = {
        .ap = {
            .channel   = CFG_AP_CHANNEL,
            .authmode  = WIFI_AUTH_WPA2_PSK,
            .max_connection = 4,
        }
    };
    strlcpy((char*)ap.ap.ssid, CFG_AP_SSID, sizeof(ap.ap.ssid));
    strlcpy((char*)ap.ap.password, CFG_AP_PASS, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(CFG_AP_SSID);

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();
    s_state = WIFI_ST_AP;
    ESP_LOGI(TAG, "AP started: SSID=%s  PASS=%s  IP=192.168.4.1", CFG_AP_SSID, CFG_AP_PASS);
}

void vbot_wifi_stop_ap(void)  { esp_wifi_stop(); }
wifi_state_t vbot_wifi_state(void) { return s_state; }
bool vbot_wifi_connected(void) { return s_state == WIFI_ST_CONNECTED; }
