#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ── State-driven face expressions (như Xiaozhi) ───────────────────────────────
typedef enum {
    FACE_IDLE      = 0,   // 😊 mặc định, thở nhẹ
    FACE_LISTENING = 1,   // 👂 sóng âm nhấp nháy
    FACE_THINKING  = 2,   // 🤔 mắt nhìn trên
    FACE_SPEAKING  = 3,   // 🗣  miệng mở đóng
    FACE_HAPPY     = 4,   // 😄 hạnh phúc
    FACE_SURPRISED = 5,   // 😮 bất ngờ
    FACE_SLEEPY    = 6,   // 😴 buồn ngủ
    FACE_ERROR     = 7,   // ❌ lỗi
} face_expr_t;

esp_err_t  vbot_display_init(void);
void       vbot_display_set_face(face_expr_t expr);
void       vbot_display_show_text(const char *line1, const char *line2);
void       vbot_display_show_wave(const int16_t *pcm, size_t n); // spectrum
void       vbot_display_clear(void);
void       vbot_display_tick(void);   // call periodically for animation
bool       vbot_display_available(void);
