// laser.c
#include "laser.h"
#include "laser_math.h"
#include "board_pins.h"
#include "app_config.h"

#include "driver/mcpwm_prelude.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "laser";

static mcpwm_timer_handle_t s_timer;
static mcpwm_oper_handle_t  s_oper[3];
static mcpwm_cmpr_handle_t  s_cmpa[3];
static mcpwm_cmpr_handle_t  s_cmpb[3];
static mcpwm_gen_handle_t   s_gen[3];
static SemaphoreHandle_t    s_mux;

static const int s_gpio[3] = { PIN_EN_R, PIN_EN_G, PIN_EN_B };

esp_err_t laser_init(void)
{
    s_mux = xSemaphoreCreateMutex();
    if (!s_mux) {
        return ESP_ERR_NO_MEM;
    }

    mcpwm_timer_config_t timer_cfg = {
        .group_id = MCPWM_GROUP_ID,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_CLK_HZ,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = TC_PERIOD_TICKS,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_cfg, &s_timer));

    for (int c = 0; c < 3; c++) {
        mcpwm_operator_config_t oper_cfg = { .group_id = MCPWM_GROUP_ID };
        ESP_ERROR_CHECK(mcpwm_new_operator(&oper_cfg, &s_oper[c]));
        ESP_ERROR_CHECK(mcpwm_operator_connect_timer(s_oper[c], s_timer));

        mcpwm_comparator_config_t cmp_cfg = { .flags.update_cmp_on_tez = true };
        ESP_ERROR_CHECK(mcpwm_new_comparator(s_oper[c], &cmp_cfg, &s_cmpa[c]));
        ESP_ERROR_CHECK(mcpwm_new_comparator(s_oper[c], &cmp_cfg, &s_cmpb[c]));
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(s_cmpa[c], 0));
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(s_cmpb[c], 0));

        mcpwm_generator_config_t gen_cfg = { .gen_gpio_num = s_gpio[c] };
        ESP_ERROR_CHECK(mcpwm_new_generator(s_oper[c], &gen_cfg, &s_gen[c]));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(s_gen[c],
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_cmpa[c], MCPWM_GEN_ACTION_HIGH)));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(s_gen[c],
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_cmpb[c], MCPWM_GEN_ACTION_LOW)));

        // Hold the output low until the first laser_set.
        ESP_ERROR_CHECK(mcpwm_generator_set_force_level(s_gen[c], 0, true));
    }

    ESP_ERROR_CHECK(mcpwm_timer_enable(s_timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(s_timer, MCPWM_TIMER_START_NO_STOP));

    ESP_LOGI(TAG, "laser ready, %d Hz, %d ticks/period", TC_PWM_HZ, (int)TC_PERIOD_TICKS);
    return ESP_OK;
}

void laser_set(float r, float g, float b, float stretch)
{
    float rgb[3] = { r, g, b };
    laser_widths_t w;
    laser_compute_widths(rgb, stretch, &w);

    xSemaphoreTake(s_mux, portMAX_DELAY);

    // Stage the full set of comparators, they load together on the next TEZ.
    for (int c = 0; c < 3; c++) {
        mcpwm_comparator_set_compare_value(s_cmpa[c], w.cmpa[c]);
        mcpwm_comparator_set_compare_value(s_cmpb[c], w.cmpb[c]);
    }

    // Drive off channels low first, then release partials, then force a full
    // channel high. A full channel only exists when the others are off, so the
    // high force never coincides with another active output.
    for (int c = 0; c < 3; c++) {
        if (w.on[c] <= 0) {
            mcpwm_generator_set_force_level(s_gen[c], 0, true);
        } else if (w.on[c] < w.period) {
            mcpwm_generator_set_force_level(s_gen[c], -1, true);
        }
    }
    for (int c = 0; c < 3; c++) {
        if (w.on[c] >= w.period) {
            mcpwm_generator_set_force_level(s_gen[c], 1, true);
        }
    }

    xSemaphoreGive(s_mux);
}

void laser_emergency_off(void)
{
    for (int c = 0; c < 3; c++) {
        if (s_gen[c]) {
            mcpwm_generator_set_force_level(s_gen[c], 0, true);
        }
    }
}
