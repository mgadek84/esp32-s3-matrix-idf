/*
 * Waveshare ESP32-S3-Matrix: 8x8 WS2812 (64), GPIO14
 * Light show zsynchronizowany z bitem (timeline z analyze_bit.py).
 *
 * Odtworz MP3 dokladnie gdy zacznie sie odliczanie 3-2-1 na matrycy.
 */
#include <stdint.h>
#include <string.h>

#include "bit_timeline.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "bit_rytm";

#define LED_GPIO      14
#define LED_COUNT     64
#define MATRIX_W      8
#define MATRIX_H      8
#define FRAME_MS      20
#define COUNTDOWN_MS  3000
#define PULSE_SLOTS   12

static led_strip_handle_t s_strip;
static uint8_t s_buf[LED_COUNT][3];
static uint8_t s_work[LED_COUNT][3];

typedef struct {
    uint32_t t_end_ms;
    uint8_t r, g, b;
    uint8_t kind;
    uint8_t param;
} active_pulse_t;

static active_pulse_t s_pulses[PULSE_SLOTS];

typedef enum {
    MOOD_INTRO = 0,
    MOOD_DROP,
    MOOD_VERSE,
    MOOD_CHORUS,
} mood_t;

static mood_t s_mood = MOOD_INTRO;
static size_t s_evt_idx;
static uint32_t s_phase;

static int xy_to_i(int x, int y)
{
    if (y < 0 || y >= MATRIX_H || x < 0 || x >= MATRIX_W) {
        return -1;
    }
    return y * MATRIX_W + x;
}

static uint8_t clamp_u8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

static void buffer_clear(void)
{
    memset(s_buf, 0, sizeof(s_buf));
}

static void buffer_add_px(int idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (idx < 0 || idx >= LED_COUNT) {
        return;
    }
    int nr = s_buf[idx][0] + r;
    int ng = s_buf[idx][1] + g;
    int nb = s_buf[idx][2] + b;
    s_buf[idx][0] = clamp_u8(nr);
    s_buf[idx][1] = clamp_u8(ng);
    s_buf[idx][2] = clamp_u8(nb);
}

static void buffer_add_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LED_COUNT; i++) {
        buffer_add_px(i, r, g, b);
    }
}

static void push_pulse(uint32_t now_ms, uint8_t kind, uint8_t r, uint8_t g, uint8_t b, uint8_t param)
{
    uint32_t dur = 90;
    switch (kind) {
    case EVT_KICK:
        dur = 110;
        break;
    case EVT_SNARE:
        dur = 85;
        break;
    case EVT_BASS:
        dur = 160;
        break;
    case EVT_HAT:
        dur = 55;
        break;
    case EVT_DROP:
        dur = 400;
        break;
    case EVT_FLASH:
        dur = 320;
        break;
    case EVT_BUILD:
        dur = 500;
        break;
    default:
        dur = 140;
        break;
    }
    for (int i = 0; i < PULSE_SLOTS; i++) {
        if (s_pulses[i].t_end_ms <= now_ms) {
            s_pulses[i].t_end_ms = now_ms + dur;
            s_pulses[i].r = r;
            s_pulses[i].g = g;
            s_pulses[i].b = b;
            s_pulses[i].kind = kind;
            s_pulses[i].param = param;
            return;
        }
    }
}

static void effect_kick(uint8_t r, uint8_t g, uint8_t b, uint8_t param)
{
    uint8_t gain = clamp_u8(20 + (param >> 1));
    buffer_add_all(r, g, b);
    (void)gain;
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            buffer_add_px(xy_to_i(x, y), r, 0, 4);
        }
    }
}

static void effect_snare(uint8_t r, uint8_t g, uint8_t b)
{
    for (int y = 0; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            if ((x + y) % 2 == 0) {
                buffer_add_px(xy_to_i(x, y), r, g, b);
            } else {
                buffer_add_px(xy_to_i(x, y), r >> 1, g >> 2, 0);
            }
        }
    }
}

static void effect_bass(uint8_t r, uint8_t g, uint8_t b, uint32_t phase)
{
    int row = (phase / 6) % MATRIX_H;
    for (int x = 0; x < MATRIX_W; x++) {
        for (int y = MATRIX_H - 1; y >= MATRIX_H - 3; y--) {
            buffer_add_px(xy_to_i(x, y), r, g, b);
        }
        buffer_add_px(xy_to_i(x, row), r >> 1, g, b >> 1);
    }
}

static void effect_hat(uint8_t r, uint8_t g, uint8_t b, uint8_t param)
{
    int n = 6 + (param % 6);
    for (int k = 0; k < n; k++) {
        int x = esp_random() % MATRIX_W;
        int y = esp_random() % 3;
        buffer_add_px(xy_to_i(x, y), r, g, b);
        buffer_add_px(xy_to_i((x + 1) % MATRIX_W, y), r >> 1, g, b);
    }
}

static void effect_drop(uint8_t r, uint8_t g, uint8_t b, uint32_t phase)
{
    for (int y = 0; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            int d = (x + y + (phase / 8)) % 6;
            if (d < 2) {
                buffer_add_px(xy_to_i(x, y), r, g >> 1, b);
            } else {
                buffer_add_px(xy_to_i(x, y), r >> 1, 0, b >> 1);
            }
        }
    }
}

static void effect_break(uint8_t r, uint8_t g, uint8_t b, uint32_t phase)
{
    int cx = (phase / 10) % MATRIX_W;
    for (int y = 0; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            int dist = (x > cx) ? (x - cx) : (cx - x);
            uint8_t fade = clamp_u8((int)b - dist * 4);
            buffer_add_px(xy_to_i(x, y), 0, g >> 1, fade);
            (void)r;
        }
    }
}

static void effect_build(uint8_t r, uint8_t g, uint8_t b, uint32_t phase)
{
    int h = 1 + (phase / 120) % MATRIX_H;
    for (int y = MATRIX_H - h; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            buffer_add_px(xy_to_i(x, y), r, g, b);
        }
    }
}

static void effect_flash(uint8_t r, uint8_t g, uint8_t b, uint32_t phase)
{
    int shift = (phase / 5) % MATRIX_W;
    for (int y = 0; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            if (((x + shift) % 4) < 2) {
                buffer_add_px(xy_to_i(x, y), r, g, b);
            }
        }
    }
}

static void render_active_pulses(uint32_t now_ms)
{
    for (int i = 0; i < PULSE_SLOTS; i++) {
        if (s_pulses[i].t_end_ms <= now_ms) {
            continue;
        }
        uint32_t left = s_pulses[i].t_end_ms - now_ms;
        uint8_t fade = clamp_u8((int)(left > 200 ? 255 : (left * 255 / 200)));
        uint8_t r = ((int)s_pulses[i].r * fade) / 255;
        uint8_t g = ((int)s_pulses[i].g * fade) / 255;
        uint8_t b = ((int)s_pulses[i].b * fade) / 255;

        switch (s_pulses[i].kind) {
        case EVT_KICK:
            effect_kick(r, g, b, s_pulses[i].param);
            break;
        case EVT_SNARE:
            effect_snare(r, g, b);
            break;
        case EVT_BASS:
            effect_bass(r, g, b, s_phase);
            break;
        case EVT_HAT:
            effect_hat(r, g, b, s_pulses[i].param);
            break;
        case EVT_DROP:
            effect_drop(r, g, b, s_phase);
            break;
        case EVT_BREAK:
            effect_break(r, g, b, s_phase);
            break;
        case EVT_BUILD:
            effect_build(r, g, b, s_phase);
            break;
        case EVT_FLASH:
            effect_flash(r, g, b, s_phase);
            break;
        default:
            buffer_add_all(r >> 2, g >> 2, b >> 2);
            break;
        }
    }
}

static void render_ambient(uint32_t t_ms)
{
    uint32_t phase = s_phase;
    uint8_t br = 6, bg = 0, bb = 14;

    switch (s_mood) {
    case MOOD_DROP:
        br = 14 + (uint8_t)((phase / 13) % 10);
        bg = 0;
        bb = 6;
        break;
    case MOOD_CHORUS:
        br = 12;
        bg = 4;
        bb = 0;
        break;
    case MOOD_VERSE:
        br = 0;
        bg = 8;
        bb = 16;
        break;
    default:
        br = 6;
        bg = 0;
        bb = 18;
        break;
    }

    for (int y = 0; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            int v = (x * 3 + y * 5 + (int)(phase / 11) + (int)(t_ms / 200)) & 7;
            uint8_t add = (v < 2) ? br : (v < 4) ? bg : bb;
            if (s_mood == MOOD_DROP) {
                buffer_add_px(xy_to_i(x, y), add, 0, add >> 2);
            } else if (s_mood == MOOD_CHORUS) {
                buffer_add_px(xy_to_i(x, y), add, add >> 1, 0);
            } else {
                buffer_add_px(xy_to_i(x, y), 0, add >> 1, add);
            }
        }
    }
}

static void dispatch_event(uint32_t now_ms, const bit_event_t *ev)
{
    switch (ev->kind) {
    case EVT_DROP:
        s_mood = MOOD_DROP;
        break;
    case EVT_FLASH:
        s_mood = MOOD_CHORUS;
        break;
    case EVT_BREAK:
        s_mood = MOOD_VERSE;
        break;
    case EVT_BUILD:
        s_mood = MOOD_INTRO;
        break;
    default:
        break;
    }
    push_pulse(now_ms, ev->kind, ev->r, ev->g, ev->b, ev->param);
}

static void poll_timeline(uint32_t t_ms)
{
    while (s_evt_idx < BIT_EVENT_COUNT && BIT_TIMELINE[s_evt_idx].ms <= t_ms) {
        dispatch_event(t_ms, &BIT_TIMELINE[s_evt_idx]);
        s_evt_idx++;
    }
}

static void render_countdown(uint32_t t_ms)
{
    buffer_clear();
    int step = (int)((COUNTDOWN_MS - t_ms) / 1000);
    if (step < 1 || step > 3) {
        return;
    }
    uint8_t r = (step == 3) ? 8 : (step == 2) ? 20 : 32;
    uint8_t g = (step == 3) ? 0 : (step == 2) ? 8 : 16;
    uint8_t b = (step == 3) ? 20 : (step == 2) ? 0 : 0;
    int rows = step;
    for (int y = MATRIX_H - rows; y < MATRIX_H; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            buffer_add_px(xy_to_i(x, y), r, g, b);
        }
    }
}

static void flush_strip(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        led_strip_set_pixel(s_strip, i, s_buf[i][0], s_buf[i][1], s_buf[i][2]);
    }
    led_strip_refresh(s_strip);
}

static void fade_out(void)
{
    for (int step = 0; step < 30; step++) {
        for (int i = 0; i < LED_COUNT; i++) {
            s_buf[i][0] = (s_buf[i][0] * 8) / 10;
            s_buf[i][1] = (s_buf[i][1] * 8) / 10;
            s_buf[i][2] = (s_buf[i][2] * 8) / 10;
        }
        flush_strip();
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);
}

void app_main(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt, &s_strip));
    led_strip_clear(s_strip);
    led_strip_refresh(s_strip);

    memset(s_pulses, 0, sizeof(s_pulses));
    s_evt_idx = 0;

    ESP_LOGI(TAG, "Bit Rytm — %d ms, %d BPM, %d zdarzen", BIT_DURATION_MS, BIT_TEMPO_BPM, BIT_EVENT_COUNT);
    ESP_LOGI(TAG, "Wlacz MP3 gdy zobaczysz odliczanie 3-2-1 na LED");

    const int64_t t0_us = esp_timer_get_time();

    while (1) {
        int64_t elapsed_us = esp_timer_get_time() - t0_us;
        uint32_t t_ms = (uint32_t)(elapsed_us / 1000);

        buffer_clear();

        if (t_ms < COUNTDOWN_MS) {
            render_countdown(COUNTDOWN_MS - t_ms);
        } else {
            uint32_t song_ms = t_ms - COUNTDOWN_MS;
            if (song_ms <= BIT_DURATION_MS) {
                poll_timeline(song_ms);
                render_ambient(song_ms);
                render_active_pulses(song_ms);
            } else {
                fade_out();
                ESP_LOGI(TAG, "Koniec bitu — restart za 2s");
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            }
        }

        flush_strip();
        s_phase += FRAME_MS;
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}
