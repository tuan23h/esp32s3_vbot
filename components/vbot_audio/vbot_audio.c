/*
 * vbot_audio.c
 * INMP441 (I2S mic) + PCM5102 (I2S DAC) + energy VAD
 * Pins và params đọc từ vbot_config tại runtime.
 */
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "vbot_audio.h"
#include "vbot_config.h"

static const char *TAG = "AUDIO";

// ─────────────────────────────────────────────────────────────────────────────
//  SHARED
// ─────────────────────────────────────────────────────────────────────────────
#define SAMPLE_RATE   16000U
#define MIC_CHUNK_MS  240
#define MIC_CHUNK_N   (SAMPLE_RATE * MIC_CHUNK_MS / 1000)  // 3840 samples
#define MIC_CHUNK_B   (MIC_CHUNK_N * 2)                    // bytes int16

// ─────────────────────────────────────────────────────────────────────────────
//  MIC  (INMP441)
// ─────────────────────────────────────────────────────────────────────────────
static i2s_chan_handle_t  s_mic_chan = NULL;
static TaskHandle_t       s_mic_task = NULL;
static volatile bool      s_mic_running = false;
static volatile bool      s_mic_muted   = false;
static volatile float     s_last_rms    = 0.0f;
static audio_chunk_cb_t   s_chunk_cb    = NULL;

/* 32-bit read buf (INMP441 sends 24-bit in 32-bit frame) */
static int32_t  s_mic32[MIC_CHUNK_N];
static int16_t  s_mic16[MIC_CHUNK_N];

static void convert32to16(const int32_t *src, int16_t *dst, size_t n, uint8_t gain_shift)
{
    for (size_t i = 0; i < n; i++) {
        /* INMP441: data left-justified, bits 31..8 valid */
        int32_t s = (src[i] >> 8) >> 8;    /* → 16-bit signed */
        s = s << gain_shift;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        dst[i] = (int16_t)s;
    }
}

static float calc_rms(const int16_t *buf, size_t n)
{
    if (!n) return 0;
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += (int64_t)buf[i] * buf[i];
    return sqrtf((float)sum / n);
}

static void mic_task(void *arg)
{
    vbot_config_t *c = vbot_config_get();
    size_t bytes_read;
    const size_t read_sz = MIC_CHUNK_N * sizeof(int32_t);

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); /* wait for start */
        ESP_LOGI(TAG, "Mic streaming...");

        while (s_mic_running) {
            esp_err_t err = i2s_channel_read(s_mic_chan, s_mic32,
                                              read_sz, &bytes_read,
                                              pdMS_TO_TICKS(300));
            if (err != ESP_OK || bytes_read == 0) continue;

            size_t n = bytes_read / sizeof(int32_t);
            convert32to16(s_mic32, s_mic16, n, c->mic_gain);

            s_last_rms = calc_rms(s_mic16, n);

            if (s_mic_muted) memset(s_mic16, 0, n * 2);

            if (s_chunk_cb) s_chunk_cb(s_mic16, n);
        }
        ESP_LOGI(TAG, "Mic stopped");
    }
}

esp_err_t vbot_mic_init(void)
{
    vbot_config_t *c = vbot_config_get();
    if (!c->mic_enabled) return ESP_OK;

    ESP_LOGI(TAG, "Mic init SCK=%d WS=%d SD=%d gain_shift=%d",
             c->mic_sck, c->mic_ws, c->mic_sd, c->mic_gain);

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 8; cc.dma_frame_num = 512;
    ESP_ERROR_CHECK(i2s_new_channel(&cc, NULL, &s_mic_chan));

    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_BITS_PER_SAMPLE_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = c->mic_sck, .ws = c->mic_ws,
            .dout = I2S_GPIO_UNUSED, .din = c->mic_sd,
        },
    };
    sc.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; /* INMP441 L/R pin → GND */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_mic_chan, &sc));

    xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 6, &s_mic_task, 1);
    ESP_LOGI(TAG, "Mic init OK");
    return ESP_OK;
}

esp_err_t vbot_mic_start(void)
{
    if (!s_mic_chan || s_mic_running) return ESP_OK;
    s_mic_running = true;
    ESP_ERROR_CHECK(i2s_channel_enable(s_mic_chan));
    xTaskNotifyGive(s_mic_task);
    return ESP_OK;
}

esp_err_t vbot_mic_stop(void)
{
    s_mic_running = false;
    if (s_mic_chan) i2s_channel_disable(s_mic_chan);
    return ESP_OK;
}

float  vbot_mic_get_rms(void)     { return s_last_rms; }
bool   vbot_mic_is_muted(void)    { return s_mic_muted; }
void   vbot_mic_set_mute(bool m)  { s_mic_muted = m; }
void   vbot_mic_set_chunk_cb(audio_chunk_cb_t cb) { s_chunk_cb = cb; }

// ─────────────────────────────────────────────────────────────────────────────
//  SPEAKER  (PCM5102 / MAX98357)
// ─────────────────────────────────────────────────────────────────────────────
static i2s_chan_handle_t s_spk_chan  = NULL;
static volatile uint8_t  s_volume   = 80;    /* 0-100 */
static volatile bool     s_playing  = false;

/* Queue: server gửi từng WAV sentence chunk */
typedef struct { uint8_t *data; size_t len; } spk_chunk_t;
static QueueHandle_t   s_spk_q   = NULL;
static EventGroupHandle_t s_spk_ev = NULL;
#define SPK_EV_STOP BIT0

/* WAV header parser */
typedef struct { uint32_t sr; uint16_t ch, bps, data_off; uint32_t data_sz; } wav_t;
static bool parse_wav(const uint8_t *b, size_t len, wav_t *w)
{
    if (len < 44 || memcmp(b,"RIFF",4) || memcmp(b+8,"WAVE",4)) return false;
    uint32_t p = 12;
    w->sr=16000; w->ch=1; w->bps=16;
    while (p+8 <= len) {
        uint32_t csz = *(uint32_t*)(b+p+4);
        if (!memcmp(b+p,"fmt ",4) && csz>=16) {
            w->ch  = *(uint16_t*)(b+p+10);
            w->sr  = *(uint32_t*)(b+p+12);
            w->bps = *(uint16_t*)(b+p+22);
        } else if (!memcmp(b+p,"data",4)) {
            w->data_off = p+8; w->data_sz = csz; return true;
        }
        if (!csz) break; p += 8+csz;
    }
    return false;
}

#define PLAY_BATCH 256
static int16_t s_stereo[PLAY_BATCH*2];

static void play_pcm(const int16_t *src, size_t mono_n, uint32_t sr, uint16_t ch)
{
    /* mono avg if stereo */
    const int16_t *mono = src; size_t mn = mono_n;
    int16_t *mb = NULL;
    if (ch == 2) {
        mb = heap_caps_malloc(mono_n/2*2, MALLOC_CAP_INTERNAL);
        if (mb) {
            for(size_t i=0;i<mono_n/2;i++) mb[i]=(int16_t)(((int32_t)src[i*2]+src[i*2+1])/2);
            mono=mb; mn=mono_n/2;
        }
    }
    /* simple resample */
    int16_t *rb = NULL;
    if (sr != SAMPLE_RATE) {
        size_t nn = (size_t)((float)mn * SAMPLE_RATE / sr);
        rb = heap_caps_malloc(nn*2, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (rb) {
            for(size_t i=0;i<nn;i++){
                float fi=(float)i*mn/nn; int lo=(int)fi,hi=lo+1<(int)mn?lo+1:lo;
                rb[i]=(int16_t)(mono[lo]*(1.f-(fi-lo))+mono[hi]*(fi-lo));
            }
            mono=rb; mn=nn;
        }
    }
    size_t off=0;
    while (off<mn && !(xEventGroupGetBits(s_spk_ev)&SPK_EV_STOP)) {
        size_t b=mn-off>PLAY_BATCH?PLAY_BATCH:mn-off;
        for(size_t i=0;i<b;i++){
            int32_t v=(int32_t)mono[off+i]*s_volume/100;
            v=v>32767?32767:v<-32768?-32768:v;
            s_stereo[i*2]=s_stereo[i*2+1]=(int16_t)v;
        }
        size_t wr=0;
        i2s_channel_write(s_spk_chan,s_stereo,b*4,&wr,pdMS_TO_TICKS(200));
        off+=b;
    }
    if (rb) heap_caps_free(rb);
    if (mb) heap_caps_free(mb);
}

static void spk_task(void *arg)
{
    ESP_LOGI(TAG, "Speaker task started");
    for (;;) {
        spk_chunk_t chunk;
        if (xQueueReceive(s_spk_q, &chunk, portMAX_DELAY) != pdTRUE) continue;
        if (xEventGroupGetBits(s_spk_ev) & SPK_EV_STOP) {
            free(chunk.data); goto drain;
        }
        s_playing = true;
        i2s_channel_enable(s_spk_chan);
        wav_t w; bool has_wav=parse_wav(chunk.data,chunk.len,&w);
        if (has_wav && w.data_off<chunk.len) {
            play_pcm((int16_t*)(chunk.data+w.data_off),(chunk.len-w.data_off)/(w.bps/8),w.sr,w.ch);
        } else if (chunk.len>=2) {
            play_pcm((int16_t*)chunk.data,chunk.len/2,SAMPLE_RATE,1);
        }
        free(chunk.data);
        i2s_channel_disable(s_spk_chan);
        s_playing=false;
        if (!(xEventGroupGetBits(s_spk_ev)&SPK_EV_STOP)) vbot_on_playback_done();
        drain:
        if (xEventGroupGetBits(s_spk_ev)&SPK_EV_STOP) {
            spk_chunk_t t; while(xQueueReceive(s_spk_q,&t,0)==pdTRUE) free(t.data);
            xEventGroupClearBits(s_spk_ev,SPK_EV_STOP);
            s_playing=false;
        }
    }
}

esp_err_t vbot_speaker_init(void)
{
    vbot_config_t *c = vbot_config_get();
    if (!c->spk_enabled) return ESP_OK;
    s_volume = c->spk_volume;

    ESP_LOGI(TAG, "Speaker init BCK=%d LRCK=%d DOUT=%d vol=%d",
             c->spk_bck, c->spk_ws, c->spk_dout, s_volume);

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    cc.dma_desc_num=8; cc.dma_frame_num=512;
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &s_spk_chan, NULL));

    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_BITS_PER_SAMPLE_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk=I2S_GPIO_UNUSED, .bclk=c->spk_bck, .ws=c->spk_ws,
            .dout=c->spk_dout, .din=I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_spk_chan, &sc));

    s_spk_q  = xQueueCreate(16, sizeof(spk_chunk_t));
    s_spk_ev = xEventGroupCreate();
    xTaskCreatePinnedToCore(spk_task, "spk", 8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Speaker init OK");
    return ESP_OK;
}

void vbot_speaker_feed(const uint8_t *data, size_t len)
{
    if (!s_spk_q || !len) return;
    uint8_t *buf = heap_caps_malloc(len, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!buf) { ESP_LOGE(TAG,"ENOMEM spk feed"); return; }
    memcpy(buf, data, len);
    spk_chunk_t c={buf,len};
    if (xQueueSend(s_spk_q,&c,pdMS_TO_TICKS(50))!=pdTRUE) {
        ESP_LOGW(TAG,"Spk queue full"); heap_caps_free(buf);
    }
}

void vbot_speaker_stop(void)
{
    if (s_spk_ev) xEventGroupSetBits(s_spk_ev, SPK_EV_STOP);
    if (s_spk_chan) i2s_zero_dma_buffer(s_spk_chan);
    s_playing = false;
}

bool vbot_speaker_is_playing(void) { return s_playing; }
void vbot_speaker_set_volume(uint8_t v) { s_volume = v>100?100:v; }

/* Test beep (sine wave) */
void vbot_speaker_beep(int freq, int dur_ms)
{
    int n = SAMPLE_RATE * dur_ms / 1000;
    uint8_t *buf = heap_caps_malloc(n*2 + 44, MALLOC_CAP_INTERNAL);
    if (!buf) return;
    /* WAV header */
    int16_t *pcm = (int16_t*)(buf+44);
    const float PI2 = 6.28318f;
    int fade = SAMPLE_RATE/100;
    for(int i=0;i<n;i++){
        float v=sinf(PI2*freq*i/SAMPLE_RATE)*28000;
        if(i<fade)   v*=(float)i/fade;
        if(i>n-fade) v*=(float)(n-i)/fade;
        pcm[i]=(int16_t)v;
    }
    /* build WAV header */
    memcpy(buf,"RIFF",4); *(uint32_t*)(buf+4)=36+n*2;
    memcpy(buf+8,"WAVE",4); memcpy(buf+12,"fmt ",4);
    *(uint32_t*)(buf+16)=16; *(uint16_t*)(buf+20)=1; *(uint16_t*)(buf+22)=1;
    *(uint32_t*)(buf+24)=SAMPLE_RATE; *(uint32_t*)(buf+28)=SAMPLE_RATE*2;
    *(uint16_t*)(buf+32)=2; *(uint16_t*)(buf+34)=16;
    memcpy(buf+36,"data",4); *(uint32_t*)(buf+40)=n*2;
    vbot_speaker_feed(buf, n*2+44);
    heap_caps_free(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
//  VAD
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t s_vad_speech_ms  = 0;
static uint32_t s_vad_silence_ms = 0;
static bool     s_vad_fired      = false;

void vbot_vad_reset(void)
{ s_vad_speech_ms=0; s_vad_silence_ms=0; s_vad_fired=false; }

void vbot_vad_feed(float rms, uint32_t chunk_ms)
{
    if (s_vad_fired) return;
    vbot_config_t *c = vbot_config_get();
    if (rms > c->vad_rms_threshold) {
        s_vad_speech_ms  += chunk_ms;
        s_vad_silence_ms  = 0;
    } else {
        s_vad_silence_ms += chunk_ms;
        if (s_vad_speech_ms  >= c->vad_min_speech_ms &&
            s_vad_silence_ms >= c->vad_silence_ms) {
            s_vad_fired = true;
            ESP_LOGI(TAG,"VAD: silence %ums → end_speech", s_vad_silence_ms);
            vbot_on_vad_silence();
        }
    }
}
