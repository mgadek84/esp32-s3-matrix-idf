/*
 * Waveshare ESP32-S3-Matrix: 64x WS2812, data pin GPIO14
 * Prosta tecza — bez bialy (nigdy R=G=B naraz na max).
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "tecza";

#define LED_GPIO   14
#define LED_COUNT  64

static led_strip_handle_t s_strip;

static void all_pixels(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LED_COUNT; i++) {
        led_strip_set_pixel(s_strip, i, r, g, b);
    }
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

    ESP_LOGI(TAG, "Tecza GPIO%d — gotowe", LED_GPIO);

    /* R, G, B osobno — unikamy bialego */
    static const uint8_t kolory[][3] = {
        { 32,  0,  0 },
        { 32, 16,  0 },
        { 32, 32,  0 },
        {  0, 32,  0 },
        {  0, 32, 32 },
        {  0,  0, 32 },
        { 24,  0, 32 },
    };

    while (1) {
        for (int i = 0; i < 7; i++) {
            all_pixels(kolory[i][0], kolory[i][1], kolory[i][2]);
            ESP_LOGI(TAG, "kolor %d", i);
            vTaskDelay(pdMS_TO_TICKS(400));
        }
    }
}
