#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ── Mic ───────────────────────────────────────────────────────────────────────
esp_err_t vbot_mic_init(void);
esp_err_t vbot_mic_start(void);
esp_err_t vbot_mic_stop(void);
float     vbot_mic_get_rms(void);   // last chunk RMS (for WebUI meter)
bool      vbot_mic_is_muted(void);
void      vbot_mic_set_mute(bool m);

// ── Speaker ───────────────────────────────────────────────────────────────────
esp_err_t vbot_speaker_init(void);
void      vbot_speaker_feed(const uint8_t *data, size_t len);
void      vbot_speaker_stop(void);
bool      vbot_speaker_is_playing(void);
void      vbot_speaker_set_volume(uint8_t vol);   // 0-100
void      vbot_speaker_beep(int freq_hz, int duration_ms);

// ── VAD ───────────────────────────────────────────────────────────────────────
void vbot_vad_reset(void);
void vbot_vad_feed(float rms, uint32_t chunk_ms);  // -> fires vbot_on_vad_silence()

// ── Callbacks (implemented in main.c) ────────────────────────────────────────
void vbot_on_vad_silence(void);
void vbot_on_playback_done(void);

// ── Audio chunk callback (mic → WebSocket) ───────────────────────────────────
// Registered by vbot_ws component; default = NULL (drops audio)
typedef void (*audio_chunk_cb_t)(const int16_t *pcm, size_t samples);
void vbot_mic_set_chunk_cb(audio_chunk_cb_t cb);
