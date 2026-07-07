// fancontrol.c
#include "fancontrol.h"
#include "board_pins.h"
#include "app_config.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "fancontrol";

#define FAN_DUTY_MAX ((1 << FAN_PWM_RES_BITS) - 1)

static float s_integ;

esp_err_t fancontrol_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_FAN_MODE,
        .timer_num = LEDC_FAN_TIMER,
        .duty_resolution = FAN_PWM_RES_BITS,
        .freq_hz = FAN_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num = PIN_FAN_PWM,
        .speed_mode = LEDC_FAN_MODE,
        .channel = LEDC_FAN_CHANNEL,
        .timer_sel = LEDC_FAN_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 1,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    ESP_LOGI(TAG, "fan ready, %d Hz", FAN_PWM_HZ);
    return ESP_OK;
}

float fancontrol_update(float temp_c, float dt)
{
    float err = temp_c - FAN_TARGET_C;
    float unclamped = FAN_KP * err + FAN_KI * (s_integ + err * dt);

    float u = unclamped;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;

    // Integrate only when not saturated.
    if (u == unclamped) {
        s_integ += err * dt;
    }

    // Linearize fan response
    float duty = u > 0.0f ? FAN_KNEE_DUTY + u * (1.0f - FAN_KNEE_DUTY) : 0.0f;

    uint32_t reg = (uint32_t)(duty * FAN_DUTY_MAX);
    ledc_set_duty(LEDC_FAN_MODE, LEDC_FAN_CHANNEL, reg);
    ledc_update_duty(LEDC_FAN_MODE, LEDC_FAN_CHANNEL);

    return duty;
}
