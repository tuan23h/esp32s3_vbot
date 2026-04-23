/*
 * vbot_display.c
 *
 * Hỗ trợ:
 *   SSD1306  — I2C OLED 128×64  (monochrome, u8g2)
 *   ST7789   — SPI TFT 240×240 / 240×320
 *   ST77916  — SPI TFT 360×360 (round)
 *   ST7735   — SPI TFT 128×160
 *
 * Face animations: vẽ trực tiếp bằng primitive (không cần font file lớn)
 * Mục tiêu: giống Xiaozhi — mắt/miệng animate theo trạng thái
 *
 * NOTE: SSD1306 dùng esp_lcd_panel_io_i2c + esp_lcd_panel_ssd1306 (IDF built-in)
 *       ST7789 dùng esp_lcd_panel_st7789 (IDF built-in)
 *       ST77916 dùng custom init sequence (tương tự ST7789)
 */

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_timer.h"
#include "vbot_display.h"
#include "vbot_config.h"

static const char *TAG = "DISP";

// ── LCD panel handle ──────────────────────────────────────────────────────────
static esp_lcd_panel_handle_t s_panel   = NULL;
static esp_lcd_panel_io_handle_t s_io   = NULL;
static int s_width = 240, s_height = 240;
static bool s_available = false;
static display_type_t s_type = DISP_NONE;

// ── Framebuffer (16-bit RGB565, PSRAM) ───────────────────────────────────────
static uint16_t *s_fb      = NULL;  /* full-screen fb */
static uint16_t *s_dirty   = NULL;  /* dirty-line flags (row bitmask) */
#define FB_SIZE  (s_width * s_height * 2)

// ── Colors (RGB565) ───────────────────────────────────────────────────────────
#define C_BG       0x0000   /* black */
#define C_WHITE    0xFFFF
#define C_ACCENT   0x1F9F   /* cyan */
#define C_GREEN    0x07E0
#define C_AMBER    0xFD00
#define C_RED      0xF800
#define C_BLUE     0x001F
#define C_GRAY     0x4208
#define C_DKGRAY   0x18C3

// ── Animation state ───────────────────────────────────────────────────────────
static face_expr_t s_face    = FACE_IDLE;
static uint32_t    s_tick    = 0;
static float       s_blink_t = 0;
static float       s_mouth_t = 0;
static float       s_wave[32] = {0};

// ── Draw primitives ───────────────────────────────────────────────────────────
static inline void fb_set(int x, int y, uint16_t c)
{
    if (x < 0 || x >= s_width || y < 0 || y >= s_height) return;
    s_fb[y * s_width + x] = __builtin_bswap16(c); /* BE for LCD */
}

static void fb_fill(uint16_t c)
{
    uint16_t bc = __builtin_bswap16(c);
    for (int i = 0; i < s_width * s_height; i++) s_fb[i] = bc;
}

static void fb_rect(int x, int y, int w, int h, uint16_t c, bool fill)
{
    for (int row = y; row < y+h; row++)
        for (int col = x; col < x+w; col++) {
            if (fill || row==y || row==y+h-1 || col==x || col==x+w-1)
                fb_set(col, row, c);
        }
}

static void fb_circle(int cx, int cy, int r, uint16_t c, bool fill)
{
    for (int y = cy-r; y <= cy+r; y++)
        for (int x = cx-r; x <= cx+r; x++) {
            float d = sqrtf((x-cx)*(x-cx)+(y-cy)*(y-cy));
            if (fill ? d<=r : fabsf(d-r)<1.2f) fb_set(x, y, c);
        }
}

/* Filled rounded rect */
static void fb_rrect(int x, int y, int w, int h, int r, uint16_t c)
{
    fb_rect(x+r, y, w-2*r, h, c, true);
    fb_rect(x, y+r, r, h-2*r, c, true);
    fb_rect(x+w-r, y+r, r, h-2*r, c, true);
    fb_circle(x+r,   y+r,   r, c, true);
    fb_circle(x+w-r, y+r,   r, c, true);
    fb_circle(x+r,   y+h-r, r, c, true);
    fb_circle(x+w-r, y+h-r, r, c, true);
}

/* Draw arc / bezier for mouth */
static void fb_arc(int cx, int cy, int rx, int ry, float a0, float a1, uint16_t c, int thick)
{
    for (float a = a0; a <= a1; a += 0.02f) {
        int x = (int)(cx + rx * cosf(a));
        int y = (int)(cy + ry * sinf(a));
        fb_circle(x, y, thick/2+1, c, true);
    }
}

/* Flush framebuffer to display */
static void fb_flush(void)
{
    if (!s_panel) return;
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_width, s_height, s_fb);
}

// ── Face drawing ──────────────────────────────────────────────────────────────
#define EYE_RX   20
#define EYE_RY   22
#define EYE_LX   (s_width/2 - 45)
#define EYE_LY   (s_height/2 - 15)
#define EYE_RX2  (s_width/2 + 45)
#define EYE_RY2  (s_height/2 - 15)
#define MOUTH_CX (s_width/2)
#define MOUTH_CY (s_height/2 + 40)

static void draw_face_idle(void)
{
    float t = s_tick * 0.04f;
    /* Gentle breathing: face center moves ±2px */
    int dy = (int)(sinf(t * 0.3f) * 2);

    /* Background gradient circle */
    float pulse = 0.85f + 0.15f * sinf(t * 0.4f);
    int bg_r = (int)(s_width * 0.42f * pulse);
    fb_circle(s_width/2, s_height/2 + dy, bg_r, 0x0841, true);

    /* Eyes (rounded rects) — blink every ~3s */
    float blink = sinf(s_blink_t);
    int eye_h = (int)(EYE_RY * 2 * (blink > -0.97f ? 1.0f : 1.0f + blink));
    eye_h = eye_h < 4 ? 4 : eye_h;

    fb_rrect(EYE_LX - EYE_RX, EYE_LY - eye_h/2 + dy,
             EYE_RX*2, eye_h, EYE_RX*2/3, C_WHITE);
    fb_rrect(EYE_RX2 - EYE_RX, EYE_RY2 - eye_h/2 + dy,
             EYE_RX*2, eye_h, EYE_RX*2/3, C_WHITE);

    /* Pupils */
    if (eye_h > 8) {
        float px = sinf(t * 0.2f) * 5;
        float py = cosf(t * 0.15f) * 3;
        fb_circle(EYE_LX  + (int)px, EYE_LY  + (int)py + dy, 8, C_BG, true);
        fb_circle(EYE_RX2 + (int)px, EYE_RY2 + (int)py + dy, 8, C_BG, true);
        /* Highlight */
        fb_circle(EYE_LX  + (int)px + 4, EYE_LY  + (int)py + dy - 5, 3, C_WHITE, true);
        fb_circle(EYE_RX2 + (int)px + 4, EYE_RY2 + (int)py + dy - 5, 3, C_WHITE, true);
    }

    /* Mouth — subtle smile */
    fb_arc(MOUTH_CX, MOUTH_CY + dy, 35, 12, 0.1f, 3.0f, C_WHITE, 3);

    /* Cheeks */
    fb_circle(EYE_LX + 5,  MOUTH_CY - 20 + dy, 12, 0xF9AE, true);
    fb_circle(EYE_RX2 - 5, MOUTH_CY - 20 + dy, 12, 0xF9AE, true);
}

static void draw_face_listening(void)
{
    float t = s_tick * 0.04f;
    /* Ripple rings around face */
    for (int i = 0; i < 3; i++) {
        float phase = t - i * 0.6f;
        float alpha = 0.3f + 0.7f * (float)(i==0);
        int r = (int)(s_width * 0.3f + sinf(phase * 2.5f) * 15 + i * 18);
        uint16_t col = i==0 ? 0x07FF : (i==1 ? 0x0600 : 0x0300);
        fb_circle(s_width/2, s_height/2, r, col, false);
    }
    /* Eyes wide open */
    fb_circle(EYE_LX,  EYE_LY,  EYE_RY, C_WHITE, true);
    fb_circle(EYE_RX2, EYE_RY2, EYE_RY, C_WHITE, true);
    fb_circle(EYE_LX,  EYE_LY,  9, C_BG, true);
    fb_circle(EYE_RX2, EYE_RY2, 9, C_BG, true);
    fb_circle(EYE_LX+3,  EYE_LY-5,  3, C_WHITE, true);
    fb_circle(EYE_RX2+3, EYE_RY2-5, 3, C_WHITE, true);
    /* Waveform mouth */
    for (int i = 0; i < 28; i++) {
        float v = s_wave[i % 32];
        int y = MOUTH_CY + (int)(v * 12);
        fb_set(MOUTH_CX - 55 + i*4, y, C_ACCENT);
        fb_set(MOUTH_CX - 55 + i*4, y+1, C_ACCENT);
    }
}

static void draw_face_thinking(void)
{
    float t = s_tick * 0.04f;
    /* Eyes look up */
    fb_circle(EYE_LX,  EYE_LY - 5,  EYE_RY, C_WHITE, true);
    fb_circle(EYE_RX2, EYE_RY2 - 5, EYE_RY, C_WHITE, true);
    fb_circle(EYE_LX - 3,  EYE_LY  - 12, 9, C_BG, true);
    fb_circle(EYE_RX2 + 3, EYE_RY2 - 12, 9, C_BG, true);
    /* Spinning dots above head */
    for (int i = 0; i < 3; i++) {
        float a = t * 2.0f + i * 2.09f;
        int x = s_width/2 + (int)(cosf(a) * 20);
        int y = s_height/2 - 80 + (int)(sinf(a) * 8);
        fb_circle(x, y, 5, C_AMBER, true);
    }
    /* Straight mouth */
    fb_rect(MOUTH_CX - 25, MOUTH_CY - 2, 50, 5, C_WHITE, true);
}

static void draw_face_speaking(void)
{
    float t = s_tick * 0.04f;
    /* Eyes normal */
    fb_rrect(EYE_LX - EYE_RX, EYE_LY - EYE_RY,
             EYE_RX*2, EYE_RY*2, EYE_RX*2/3, C_WHITE);
    fb_rrect(EYE_RX2 - EYE_RX, EYE_RY2 - EYE_RY,
             EYE_RX*2, EYE_RY*2, EYE_RX*2/3, C_WHITE);
    /* Animated mouth open/close */
    float mh = 12 + 10 * fabsf(sinf(s_mouth_t * 8));
    fb_rrect(MOUTH_CX - 30, MOUTH_CY - (int)(mh/2), 60, (int)mh, 8, C_WHITE);
    /* Teeth */
    if (mh > 14) {
        fb_rect(MOUTH_CX - 26, MOUTH_CY - (int)(mh/2), 52, 7, 0xCE59, true);
    }
    /* Sound waves from mouth */
    for (int i = 1; i <= 3; i++) {
        float ph = t - i * 0.3f;
        float a = 0.5f + 0.5f * sinf(ph * 3);
        int r = 45 + i * 15;
        if (a > 0.2f) {
            fb_arc(MOUTH_CX, MOUTH_CY, r, r*0.4f, 0.3f, 2.8f, C_ACCENT, 2);
        }
    }
}

static void draw_face_happy(void)
{
    /* Big smile, squinting eyes */
    float t = s_tick * 0.04f;
    float bounce = sinf(t * 4) * 3;
    /* Squint eyes (half-moon) */
    fb_arc(EYE_LX,  EYE_LY  + (int)bounce, EYE_RX+4, EYE_RY+4, 3.14f, 6.28f, C_WHITE, 4);
    fb_arc(EYE_RX2, EYE_RY2 + (int)bounce, EYE_RX+4, EYE_RY+4, 3.14f, 6.28f, C_WHITE, 4);
    /* Big open smile */
    fb_arc(MOUTH_CX, MOUTH_CY + (int)bounce, 45, 18, 0.05f, 3.09f, C_WHITE, 5);
    /* Sparkles */
    for (int i = 0; i < 5; i++) {
        float a = t * 1.5f + i * 1.26f;
        int x = s_width/2  + (int)(cosf(a) * 85);
        int y = s_height/2 + (int)(sinf(a) * 85);
        float sz = 2 + sinf(t*3+i)*1.5f;
        fb_circle(x, y, (int)sz, C_AMBER, true);
    }
}

static void draw_face_error(void)
{
    /* X eyes, frown */
    float t = s_tick * 0.04f;
    float shake = sinf(t * 20) * 4;
    int ox = (int)shake;
    /* X left eye */
    fb_circle(EYE_LX+ox,  EYE_LY,  EYE_RY, 0x1800, true);
    fb_arc(EYE_LX+ox,  EYE_LY, EYE_RY-3, EYE_RY-3, 0.7f, 2.4f, C_RED, 4);
    fb_arc(EYE_LX+ox,  EYE_LY, EYE_RY-3, EYE_RY-3, 3.9f, 5.5f, C_RED, 4);
    /* X right eye */
    fb_circle(EYE_RX2+ox, EYE_RY2, EYE_RY, 0x1800, true);
    fb_arc(EYE_RX2+ox, EYE_RY2, EYE_RY-3, EYE_RY-3, 0.7f, 2.4f, C_RED, 4);
    fb_arc(EYE_RX2+ox, EYE_RY2, EYE_RY-3, EYE_RY-3, 3.9f, 5.5f, C_RED, 4);
    /* Frown */
    fb_arc(MOUTH_CX+ox, MOUTH_CY + 15, 35, 15, 3.3f, 6.1f, C_RED, 4);
}

// ── Display tick (animation) ──────────────────────────────────────────────────
void vbot_display_tick(void)
{
    if (!s_available) return;
    s_tick++;

    /* Blink counter */
    s_blink_t += 0.05f;
    if (s_blink_t > 6.28f * 3) s_blink_t -= 6.28f * 3;

    /* Mouth wobble for speaking */
    s_mouth_t += 0.08f;

    fb_fill(C_BG);

    switch (s_face) {
    case FACE_IDLE:      draw_face_idle();      break;
    case FACE_LISTENING: draw_face_listening(); break;
    case FACE_THINKING:  draw_face_thinking();  break;
    case FACE_SPEAKING:  draw_face_speaking();  break;
    case FACE_HAPPY:     draw_face_happy();     break;
    case FACE_ERROR:     draw_face_error();     break;
    default:             draw_face_idle();      break;
    }

    fb_flush();
}

// ── Init helpers ──────────────────────────────────────────────────────────────
static esp_err_t init_ssd1306(vbot_config_t *c)
{
#if CONFIG_LCD_ENABLE_DEBUG_LOG
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_DEBUG);
#endif
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = c->disp_i2c_sda, .scl_io_num = c->disp_i2c_scl,
        .sda_pullup_en = true, .scl_pullup_en = true,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_cfg));
    ESP_ERROR_CHECK(i2s_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, NULL));

    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = c->disp_i2c_addr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits  = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0,
                                              &io_cfg, &s_io));
    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = c->disp_rst, .bits_per_pixel = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(s_io, &dev_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    s_width = 128; s_height = 64;
    return ESP_OK;
}

static esp_err_t init_st7789(vbot_config_t *c)
{
    /* SPI bus */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = c->disp_spi_mosi, .sclk_io_num = c->disp_spi_clk,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = s_width * 80 * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = c->disp_dc, .cs_gpio_num = c->disp_spi_cs,
        .pclk_hz = 40*1000*1000, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .spi_mode = 0, .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &s_io));

    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = c->disp_rst,
        .rgb_endian     = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &dev_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    if (c->disp_rotate) esp_lcd_panel_mirror(s_panel, true, true);
    if (c->disp_bl >= 0) {
        gpio_set_direction(c->disp_bl, GPIO_MODE_OUTPUT);
        gpio_set_level(c->disp_bl, 1);
    }
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────
esp_err_t vbot_display_init(void)
{
    vbot_config_t *c = vbot_config_get();
    s_type = c->disp_type;
    if (s_type == DISP_NONE) { ESP_LOGI(TAG,"No display configured"); return ESP_OK; }

    s_width  = c->disp_width;
    s_height = c->disp_height;
    ESP_LOGI(TAG, "Display init type=%d %dx%d", s_type, s_width, s_height);

    /* Alloc framebuffer in PSRAM */
    s_fb = heap_caps_malloc(s_width * s_height * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) { ESP_LOGE(TAG,"FB alloc failed"); return ESP_ERR_NO_MEM; }
    memset(s_fb, 0, s_width * s_height * 2);

    esp_err_t err = ESP_FAIL;
    switch (s_type) {
    case DISP_SSD1306: err = init_ssd1306(c); break;
    case DISP_ST7789:
    case DISP_ST77916:  /* same init as ST7789, different resolution */
    case DISP_ST7735:   err = init_st7789(c); break;
    default: break;
    }
    if (err == ESP_OK) {
        s_available = true;
        ESP_LOGI(TAG, "Display ready %dx%d", s_width, s_height);
    }
    return err;
}

void vbot_display_set_face(face_expr_t expr) { s_face = expr; }

void vbot_display_show_text(const char *l1, const char *l2)
{
    /* Simple text via framebuffer — skipped for space, implement via lvgl or u8g2 */
    (void)l1; (void)l2;
}

void vbot_display_show_wave(const int16_t *pcm, size_t n)
{
    /* Downsample to 32 bins for mouth waveform */
    size_t step = n / 32;
    for (int i = 0; i < 32; i++) {
        float sum = 0;
        for (size_t j = 0; j < step; j++) sum += fabsf(pcm[i*step+j]);
        s_wave[i] = sum / step / 32768.0f;
    }
}

void vbot_display_clear(void)
{ if(s_fb){memset(s_fb,0,s_width*s_height*2); fb_flush();} }
bool vbot_display_available(void) { return s_available; }
