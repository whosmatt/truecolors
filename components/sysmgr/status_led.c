// status_led.c
// overengineered status LED controller
#include "status_led.h"

#include <math.h>
#include "board_pins.h"
#include "app_config.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "status_led";

#define LED_PI            3.14159265358979f
#define STATUS_LED_MAX    0.55f   // master scale
#define BREATHE_FLOOR     0.25f   // apple style minimum for low breathe state

typedef enum { PAT_SOLID, PAT_BREATHE, PAT_PULSE, PAT_HEARTBEAT } led_pattern_t;

typedef struct {
    uint8_t r, g, b;
    led_pattern_t pattern;
    float period_s;
} led_anim_t;

// Indexed by led_state_t.
static const led_anim_t k_states[LED_STATE_COUNT] = {
    [LED_BOOT]              = {255, 255, 255, PAT_BREATHE, 3.0f},
    [LED_AP_PROVISION]      = {  0,  60, 255, PAT_BREATHE, 1.0f},
    [LED_CONNECTING]        = {  0,  60, 255, PAT_BREATHE, 0.5f},
    [LED_CONNECTED_IDLE]    = {  0,  60,  10, PAT_BREATHE, 4.0f},
    [LED_CONNECTED_RUNNING] = {  0,  10,   1, PAT_SOLID,   1.0f},
    [LED_WARNING]           = {255, 110,   0, PAT_PULSE,   1.5f},
    [LED_ERROR]             = {255,   0,   0, PAT_PULSE,   0.8f},
};

static led_strip_handle_t s_strip;
static volatile led_state_t s_state = LED_BOOT;

// Intensity envelope in [0,1] for a given pattern at phase in [0,1).
static float pattern_intensity(led_pattern_t p, float phase)
{
    switch (p) {
    case PAT_SOLID:
        return 1.0f;
    case PAT_BREATHE: {
        float v = 0.5f * (1.0f - cosf(2.0f * LED_PI * phase));  // sine in/out
        return BREATHE_FLOOR + (1.0f - BREATHE_FLOOR) * v;
    }
    case PAT_PULSE:
        return expf(-3.0f * phase);                              // sharp on -> fade
    case PAT_HEARTBEAT: {
        float a = expf(-25.0f * phase);
        float b = expf(-25.0f * fabsf(phase - 0.18f));
        float v = a + b;
        return v > 1.0f ? 1.0f : v;
    }
    }
    return 1.0f;
}

static void anim_task(void *arg)
{
    (void)arg;
    const float dt = 1.0f / STATUS_LED_HZ;
    float phase = 0.0f;
    led_state_t last = LED_STATE_COUNT;  // force first-iteration reset

    for (;;) {
        led_state_t st = s_state;
        if (st != last) {
            phase = 0.0f;
            last = st;
        }
        const led_anim_t *a = &k_states[st];

        float i = pattern_intensity(a->pattern, phase) * STATUS_LED_MAX;
        uint8_t r = (uint8_t)(a->r * i);
        uint8_t g = (uint8_t)(a->g * i);
        uint8_t b = (uint8_t)(a->b * i);

        led_strip_set_pixel(s_strip, 0, r, g, b);
        led_strip_refresh(s_strip);

        phase += dt / a->period_s;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / STATUS_LED_HZ));
    }
}

esp_err_t status_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = PIN_LED_DATA,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10 MHz
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        return err;
    }
    led_strip_clear(s_strip);

    BaseType_t ok = xTaskCreatePinnedToCore(anim_task, "status_led", 3072, NULL,
                                            4, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "anim task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "status LED ready");
    return ESP_OK;
}

void status_led_set_state(led_state_t state)
{
    if (state < LED_STATE_COUNT) {
        s_state = state;
    }
}
