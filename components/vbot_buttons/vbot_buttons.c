#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "vbot_buttons.h"
#include "vbot_config.h"

static const char *TAG   = "BTN";
static btn_cb_t    s_cb  = NULL;

#define DEBOUNCE_MS  50
#define HOLD_MS      500
#define REPEAT_MS    200

typedef struct {
    int8_t  gpio;
    bool    active_low;
    bool    last;
    bool    pressed;
    uint32_t press_time;
    uint32_t hold_fire;
} btn_state_t;

static btn_state_t s_btns[BTN_COUNT];

static void btn_task(void *arg)
{
    for (;;) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        for (int i = 0; i < BTN_COUNT; i++) {
            btn_state_t *b = &s_btns[i];
            if (b->gpio < 0) continue;

            bool raw    = gpio_get_level(b->gpio);
            bool active = b->active_low ? !raw : raw;

            if (active && !b->pressed) {
                /* Debounce */
                vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
                raw    = gpio_get_level(b->gpio);
                active = b->active_low ? !raw : raw;
                if (!active) continue;

                b->pressed    = true;
                b->press_time = now;
                b->hold_fire  = now + HOLD_MS;
                if (s_cb) s_cb((btn_id_t)i, BTN_EVT_PRESS);
                ESP_LOGI(TAG, "BTN%d PRESS", i);

            } else if (!active && b->pressed) {
                b->pressed = false;
                if (s_cb) s_cb((btn_id_t)i, BTN_EVT_RELEASE);

            } else if (active && b->pressed && now >= b->hold_fire) {
                /* Hold / repeat */
                b->hold_fire = now + REPEAT_MS;
                if (s_cb) s_cb((btn_id_t)i, BTN_EVT_HOLD);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t vbot_buttons_init(void)
{
    vbot_config_t *c = vbot_config_get();
    int8_t gpios[BTN_COUNT] = { c->btn_wake, c->btn_vol_up, c->btn_vol_down };

    for (int i = 0; i < BTN_COUNT; i++) {
        s_btns[i].gpio       = gpios[i];
        s_btns[i].active_low = c->btn_active_low;
        s_btns[i].pressed    = false;
        if (gpios[i] < 0) continue;
        gpio_config_t gc = {
            .pin_bit_mask = 1ULL << gpios[i],
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = c->btn_active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = c->btn_active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&gc);
        ESP_LOGI(TAG, "BTN%d GPIO%d active_%s", i, gpios[i], c->btn_active_low?"low":"high");
    }
    xTaskCreate(btn_task, "btn", 2048, NULL, 3, NULL);
    return ESP_OK;
}

void vbot_buttons_set_cb(btn_cb_t cb) { s_cb = cb; }
