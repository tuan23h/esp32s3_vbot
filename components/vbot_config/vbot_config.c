#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "vbot_config.h"

static const char *TAG = "CFG";
static const char *NVS_NS  = "vbot";
static const char *NVS_KEY = "cfg";

static vbot_config_t s_cfg;

esp_err_t vbot_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS erase and reinit");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t h;
    err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No config in NVS — using defaults");
        vbot_config_t def = CFG_DEFAULTS;
        s_cfg = def;
        return ESP_OK;
    }

    size_t size = sizeof(vbot_config_t);
    err = nvs_get_blob(h, NVS_KEY, &s_cfg, &size);
    nvs_close(h);

    if (err != ESP_OK || size != sizeof(vbot_config_t)) {
        ESP_LOGW(TAG, "Config size mismatch or missing — reset to defaults");
        vbot_config_t def = CFG_DEFAULTS;
        s_cfg = def;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Loaded: ssid=%s host=%s port=%d disp=%d",
             s_cfg.wifi_ssid, s_cfg.server_host,
             s_cfg.server_port, s_cfg.disp_type);
    return ESP_OK;
}

vbot_config_t *vbot_config_get(void) { return &s_cfg; }

esp_err_t vbot_config_save(const vbot_config_t *cfg)
{
    if (cfg) s_cfg = *cfg;
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    esp_err_t err = nvs_set_blob(h, NVS_KEY, &s_cfg, sizeof(vbot_config_t));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) ESP_LOGI(TAG, "Saved OK");
    else               ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t vbot_config_reset(void)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    vbot_config_t def = CFG_DEFAULTS;
    s_cfg = def;
    ESP_LOGI(TAG, "Reset to defaults");
    return ESP_OK;
}
