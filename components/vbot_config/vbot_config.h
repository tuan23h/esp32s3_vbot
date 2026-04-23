#pragma once
/*
 * vbot_config — Cấu hình toàn bộ hệ thống, lưu/đọc từ NVS.
 * Thay đổi qua Web UI, apply ngay hoặc sau restart.
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ─────────────────────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────────────────────
#define CFG_WIFI_SSID_MAX     64
#define CFG_WIFI_PASS_MAX     64
#define CFG_AP_SSID           "VBot-Config"
#define CFG_AP_PASS           "vbot1234"
#define CFG_AP_CHANNEL        1

// ─────────────────────────────────────────────────────────────────────────────
// Server
// ─────────────────────────────────────────────────────────────────────────────
#define CFG_SERVER_HOST_MAX   64
#define CFG_SERVER_PORT_DEF   3000
#define CFG_WS_PATH           "/audio"

// ─────────────────────────────────────────────────────────────────────────────
// Display type
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
    DISP_NONE    = 0,
    DISP_SSD1306 = 1,   // I2C OLED 128×64
    DISP_ST7789  = 2,   // SPI TFT 240×240 / 240×320
    DISP_ST77916 = 3,   // SPI TFT 360×360 (round)
    DISP_ST7735  = 4,   // SPI TFT 128×160
} display_type_t;

// ─────────────────────────────────────────────────────────────────────────────
// Full config struct
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    /* WiFi */
    char     wifi_ssid[CFG_WIFI_SSID_MAX];
    char     wifi_pass[CFG_WIFI_PASS_MAX];

    /* Server */
    char     server_host[CFG_SERVER_HOST_MAX];
    uint16_t server_port;

    /* Mic I2S (INMP441) */
    int8_t   mic_sck;    // BCLK
    int8_t   mic_ws;     // LRCLK / Word Select
    int8_t   mic_sd;     // Data
    uint8_t  mic_gain;   // 0–8 (left shift)
    bool     mic_enabled;

    /* Speaker I2S (PCM5102 hoặc MAX98357) */
    int8_t   spk_bck;
    int8_t   spk_ws;
    int8_t   spk_dout;
    uint8_t  spk_volume; // 0–100
    bool     spk_enabled;

    /* Display */
    display_type_t disp_type;
    uint16_t disp_width;
    uint16_t disp_height;
    bool     disp_rotate; // 180°

    /* Display SPI pins (ST7789/ST77916/ST7735) */
    int8_t   disp_spi_clk;
    int8_t   disp_spi_mosi;
    int8_t   disp_spi_cs;
    int8_t   disp_dc;
    int8_t   disp_rst;
    int8_t   disp_bl;    // backlight (-1 = không có)

    /* Display I2C pins (SSD1306) */
    int8_t   disp_i2c_sda;
    int8_t   disp_i2c_scl;
    uint8_t  disp_i2c_addr; // 0x3C hoặc 0x3D

    /* Buttons */
    int8_t   btn_wake;      // wake-up button
    int8_t   btn_vol_up;    // volume +
    int8_t   btn_vol_down;  // volume -
    bool     btn_active_low; // true = LOW khi nhấn

    /* LED */
    int8_t   led_gpio;      // WS2812 data pin
    uint8_t  led_count;     // số LED trong ring (0 = single LED)
    uint8_t  led_brightness; // 0–255

    /* VAD */
    uint16_t vad_rms_threshold; // energy threshold
    uint16_t vad_silence_ms;    // ms im lặng → end_speech
    uint16_t vad_min_speech_ms; // ms giọng tối thiểu

    /* System */
    bool     provisioned;   // đã cấu hình lần đầu chưa
    char     device_name[32];
} vbot_config_t;

// ─────────────────────────────────────────────────────────────────────────────
// Defaults (ESP32-S3 Zero + INMP441 + PCM5102)
// ─────────────────────────────────────────────────────────────────────────────
#define CFG_DEFAULTS { \
    .wifi_ssid         = "",             \
    .wifi_pass         = "",             \
    .server_host       = "192.168.1.100",\
    .server_port       = 3000,           \
    .mic_sck           = 1,  .mic_ws = 2, .mic_sd = 3, \
    .mic_gain          = 3,  .mic_enabled = true,       \
    .spk_bck           = 5,  .spk_ws = 6, .spk_dout = 4, \
    .spk_volume        = 80, .spk_enabled = true,       \
    .disp_type         = DISP_NONE,      \
    .disp_width        = 240, .disp_height = 240,       \
    .disp_rotate       = false,          \
    .disp_spi_clk      = 36, .disp_spi_mosi = 35,       \
    .disp_spi_cs       = 34, .disp_dc = 33,             \
    .disp_rst          = -1, .disp_bl = -1,             \
    .disp_i2c_sda      = 8,  .disp_i2c_scl = 9,         \
    .disp_i2c_addr     = 0x3C,           \
    .btn_wake          = 0,  .btn_vol_up = -1, .btn_vol_down = -1, \
    .btn_active_low    = true,           \
    .led_gpio          = 21, .led_count = 12, .led_brightness = 30, \
    .vad_rms_threshold = 512,            \
    .vad_silence_ms    = 1800,           \
    .vad_min_speech_ms = 400,            \
    .provisioned       = false,          \
    .device_name       = "VBot",         \
}

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────
esp_err_t       vbot_config_init(void);
vbot_config_t  *vbot_config_get(void);
esp_err_t       vbot_config_save(const vbot_config_t *cfg);
esp_err_t       vbot_config_reset(void);  // xóa về default
