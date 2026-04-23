/*
 * vbot_led.c — WS2812 ring LED effects
 * Dùng esp-idf led_strip component (IDF v5.x built-in).
 */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "esp_timer.h"
#include "vbot_led.h"
#include "vbot_config.h"

static const char *TAG = "LED";

static led_strip_handle_t s_strip = NULL;
static int                s_n     = 12;   // LED count
static uint8_t            s_bri   = 30;
static volatile led_fx_t  s_fx    = LED_FX_IDLE;
static uint32_t           s_tick  = 0;

// ── Color helpers ─────────────────────────────────────────────────────────────
typedef struct { uint8_t r, g, b; } rgb_t;

static rgb_t dim(rgb_t c, uint8_t bri)
{
    return (rgb_t){
        .r = (uint16_t)c.r * bri / 255,
        .g = (uint16_t)c.g * bri / 255,
        .b = (uint16_t)c.b * bri / 255,
    };
}

static rgb_t hsv(float h, float s, float v)
{
    float c = v * s, x = c * (1.f - fabsf(fmodf(h/60.f, 2.f) - 1.f));
    float m = v - c;
    float r=0,g=0,b=0;
    int hi=(int)(h/60)%6;
    if(hi==0){r=c;g=x;}else if(hi==1){r=x;g=c;}else if(hi==2){g=c;b=x;}
    else if(hi==3){g=x;b=c;}else if(hi==4){r=x;b=c;}else{r=c;b=x;}
    return (rgb_t){(uint8_t)((r+m)*255),(uint8_t)((g+m)*255),(uint8_t)((b+m)*255)};
}

static void set_px(int i, rgb_t c)
{
    if (i < 0 || i >= s_n || !s_strip) return;
    rgb_t d = dim(c, s_bri);
    led_strip_set_pixel(s_strip, i, d.r, d.g, d.b);
}

static void all_off(void)
{
    for(int i=0;i<s_n;i++) led_strip_set_pixel(s_strip,i,0,0,0);
}

// ── Effects ───────────────────────────────────────────────────────────────────
static void fx_idle(void)
{
    /* Breathing: gentle cyan pulse */
    float t = s_tick * 0.03f;
    float bri = 0.3f + 0.7f * (0.5f + 0.5f * sinf(t));
    rgb_t c = {(uint8_t)(20*bri), (uint8_t)(100*bri), (uint8_t)(180*bri)};
    for (int i = 0; i < s_n; i++) set_px(i, c);
}

static void fx_listening(void)
{
    /* Spinning green dot with tail */
    float t = s_tick * 0.12f;
    int head = (int)(t * s_n / (2*3.14159f)) % s_n;
    for (int i = 0; i < s_n; i++) {
        int dist = (i - head + s_n) % s_n;
        float fade = dist < 5 ? 1.0f - dist * 0.2f : 0.0f;
        set_px(i, (rgb_t){0, (uint8_t)(220*fade), (uint8_t)(80*fade)});
    }
}

static void fx_thinking(void)
{
    /* Amber pulse wave */
    float t = s_tick * 0.05f;
    for (int i = 0; i < s_n; i++) {
        float phase = t - (float)i / s_n * 2 * 3.14159f;
        float bri = 0.2f + 0.8f * (0.5f + 0.5f * sinf(phase * 2));
        set_px(i, (rgb_t){(uint8_t)(245*bri),(uint8_t)(166*bri*0.4f),0});
    }
}

static void fx_speaking(void)
{
    /* Blue wave that pulses with audio */
    float t = s_tick * 0.08f;
    for (int i = 0; i < s_n; i++) {
        float a = (float)i / s_n * 2 * 3.14159f;
        float v = 0.3f + 0.7f * (0.5f + 0.5f * sinf(a * 2 - t * 3));
        set_px(i, (rgb_t){0, (uint8_t)(80*v), (uint8_t)(255*v)});
    }
}

static void fx_happy(void)
{
    /* Rainbow chase */
    float t = s_tick * 0.08f;
    for (int i = 0; i < s_n; i++) {
        float hue = fmodf((float)i * 360.f / s_n + t * 60, 360);
        set_px(i, hsv(hue, 1.0f, 1.0f));
    }
}

static void fx_error(void)
{
    /* Slow red flash */
    float t = s_tick * 0.1f;
    float bri = 0.5f + 0.5f * sinf(t * 5);
    rgb_t c = {(uint8_t)(255*bri), 0, 0};
    for (int i = 0; i < s_n; i++) set_px(i, c);
}

static void fx_wifi(void)
{
    /* White scanner */
    float t = s_tick * 0.15f;
    int pos = (int)(t) % (s_n * 2);
    for (int i = 0; i < s_n; i++) {
        int d = abs(i - (pos < s_n ? pos : s_n*2 - pos));
        float v = d < 3 ? 1.0f - d * 0.3f : 0.05f;
        set_px(i, (rgb_t){(uint8_t)(200*v),(uint8_t)(200*v),(uint8_t)(200*v)});
    }
}

// ── Tick task ─────────────────────────────────────────────────────────────────
static void led_task(void *arg)
{
    for (;;) {
        if (!s_strip) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        s_tick++;
        switch (s_fx) {
        case LED_FX_IDLE:      fx_idle();      break;
        case LED_FX_LISTENING: fx_listening(); break;
        case LED_FX_THINKING:  fx_thinking();  break;
        case LED_FX_SPEAKING:  fx_speaking();  break;
        case LED_FX_HAPPY:     fx_happy();     break;
        case LED_FX_ERROR:     fx_error();     break;
        case LED_FX_WIFI:      fx_wifi();      break;
        case LED_FX_OFF:       all_off();      break;
        }
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(33)); /* ~30fps */
    }
}

// ── Public API ────────────────────────────────────────────────────────────────
esp_err_t vbot_led_init(void)
{
    vbot_config_t *c = vbot_config_get();
    if (c->led_gpio < 0) { ESP_LOGI(TAG,"No LED GPIO"); return ESP_OK; }

    s_n   = c->led_count  > 0 ? c->led_count : 1;
    s_bri = c->led_brightness;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = c->led_gpio,
        .max_leds         = s_n,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model        = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);

    xTaskCreate(led_task, "led", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "LED init OK GPIO=%d N=%d bri=%d", c->led_gpio, s_n, s_bri);
    return ESP_OK;
}

void vbot_led_set_fx(led_fx_t fx)       { s_fx = fx; }
void vbot_led_set_brightness(uint8_t b) { s_bri = b; }
void vbot_led_tick(void)                { /* driven by internal task */ }

void vbot_led_set_effect(const char *name)
{
    if      (!strcmp(name,"idle"))    s_fx=LED_FX_IDLE;
    else if (!strcmp(name,"listen"))  s_fx=LED_FX_LISTENING;
    else if (!strcmp(name,"think"))   s_fx=LED_FX_THINKING;
    else if (!strcmp(name,"speak"))   s_fx=LED_FX_SPEAKING;
    else if (!strcmp(name,"happy"))   s_fx=LED_FX_HAPPY;
    else if (!strcmp(name,"error"))   s_fx=LED_FX_ERROR;
    else if (!strcmp(name,"wifi"))    s_fx=LED_FX_WIFI;
    else if (!strcmp(name,"off"))     s_fx=LED_FX_OFF;
}
