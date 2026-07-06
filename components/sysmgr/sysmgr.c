// sysmgr.c
#include "sysmgr.h"
#include "sysmgr_safety.h"
#include "fancontrol.h"
#include "status_led.h"
#include "board_pins.h"
#include "app_config.h"
#include "app_events.h"
#include "appstate.h"
#include "laser.h"
#include "wifi_mgr.h"

#include <math.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "sysmgr";

_Static_assert(SF_VIN_LOW == FLAG_VIN_LOW && SF_UNDERCURRENT == FLAG_UNDERCURRENT &&
               SF_PD_LOST == FLAG_PD_LOST && SF_OVERTEMP == FLAG_OVERTEMP &&
               SF_FAN_STALL == FLAG_FAN_STALL, "safety flag bits must match app_flag_t");

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_ch224a;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali_vin;
static adc_cali_handle_t s_cali_ntc;
static temperature_sensor_handle_t s_tsens;
static pcnt_unit_handle_t s_pcnt;

static safety_state_t s_safety;
static app_wifi_mode_t s_wifi_mode = WIFI_MODE_BOOT;

// ---- CH224A ----

static bool ch224a_read(uint8_t reg, uint8_t *val)
{
    if (!s_ch224a) {
        return false;
    }
    return i2c_master_transmit_receive(s_ch224a, &reg, 1, val, 1,
                                       pdMS_TO_TICKS(50)) == ESP_OK;
}

static void read_pd(bool *pd_ok, float *current_a)
{
    uint8_t status = 0, current = 0;
    bool ok = ch224a_read(CH224A_REG_STATUS, &status) &&
              ch224a_read(CH224A_REG_CURRENT, &current);
    *pd_ok = ok && (status & CH224A_STATUS_PD_BIT);
    *current_a = ok ? (current * (float)CH224A_CURRENT_MA_PER_COUNT / 1000.0f) : 0.0f;
}

// ---- ADC ----

static float read_adc_v(adc_channel_t ch, adc_cali_handle_t cali)
{
    int64_t acc = 0;
    for (int i = 0; i < ADC_OVERSAMPLE_N; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, ch, &raw) == ESP_OK) {
            acc += raw;
        }
    }
    int avg = (int)(acc / ADC_OVERSAMPLE_N);
    int mv = 0;
    if (cali) {
        adc_cali_raw_to_voltage(cali, avg, &mv);
    }
    return mv / 1000.0f;
}

static float read_vin(void)
{
    return read_adc_v(ADC_CH_VIN, s_cali_vin) * VIN_DIVIDER_SCALE;
}

static float read_ntc(bool *valid)
{
    float v = read_adc_v(ADC_CH_NTC, s_cali_ntc);
    if (v <= NTC_V_SHORT || v >= NTC_V_OPEN) {
        *valid = false;
        return 0.0f;
    }
    *valid = true;
    float r = NTC_R_SERIES * v / (ADC_VREF_V - v);
    float tk = NTC_B_VALUE / (logf(r / NTC_R_NOMINAL) + NTC_B_VALUE / NTC_T0_K);
    return tk - 273.15f;
}

static float read_mcu_temp(void)
{
    float c = 0.0f;
    if (s_tsens) {
        temperature_sensor_get_celsius(s_tsens, &c);
    }
    return c;
}

static int read_fan_rpm(float dt)
{
    static float window_s;
    static int last_rpm;
    if (!s_pcnt || dt <= 0.0f) {
        return 0;
    }
    // Accumulate a full second of edges before computing
    window_s += dt;
    if (window_s < 1.0f) {
        return last_rpm;
    }
    int count = 0;
    pcnt_unit_get_count(s_pcnt, &count);
    pcnt_unit_clear_count(s_pcnt);
    float revs = (float)count / (2.0f * FAN_PULSES_PER_REV);
    last_rpm = (int)(revs / window_s * 60.0f);
    window_s = 0.0f;
    return last_rpm;
}

// ---- init helpers ----

static void i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT_CH224A,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_i2c_bus) != ESP_OK) {
        ESP_LOGW(TAG, "i2c bus init failed");
        return;
    }

    uint8_t addr = CH224A_ADDR_A;
    if (i2c_master_probe(s_i2c_bus, CH224A_ADDR_A, pdMS_TO_TICKS(50)) != ESP_OK) {
        addr = CH224A_ADDR_B;
        if (i2c_master_probe(s_i2c_bus, CH224A_ADDR_B, pdMS_TO_TICKS(50)) != ESP_OK) {
            ESP_LOGW(TAG, "CH224A not found at 0x22/0x23");
            return;
        }
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = CH224A_I2C_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_ch224a));
    ESP_LOGI(TAG, "CH224A at 0x%02x", addr);
}

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_SENSE };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &s_adc));

    adc_oneshot_chan_cfg_t ccfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CH_VIN, &ccfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CH_NTC, &ccfg));

    adc_cali_curve_fitting_config_t cal = {
        .unit_id = ADC_UNIT_SENSE,
        .chan = ADC_CH_VIN,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali_vin) != ESP_OK) {
        ESP_LOGW(TAG, "VIN ADC cali unavailable");
    }
    cal.chan = ADC_CH_NTC;
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali_ntc) != ESP_OK) {
        ESP_LOGW(TAG, "NTC ADC cali unavailable");
    }
}

static void tsens_init(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_tsens) == ESP_OK) {
        temperature_sensor_enable(s_tsens);
    } else {
        ESP_LOGW(TAG, "MCU temp sensor init failed");
    }
}

static void pcnt_init(void)
{
    pcnt_unit_config_t ucfg = { .high_limit = 30000, .low_limit = -1 };
    ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &s_pcnt));

    pcnt_glitch_filter_config_t gf = { .max_glitch_ns = 1000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt, &gf));

    pcnt_chan_config_t chcfg = { .edge_gpio_num = PIN_FAN_TACH, .level_gpio_num = -1 };
    pcnt_channel_handle_t chan;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &chcfg, &chan));
    // Count both edges: 2 tach pulses/rev -> 4 edges/rev of resolution.
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));

    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt));
}

static void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
}

static void on_wifi_state(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const app_wifi_evt_t *e = data;
    if (e) {
        s_wifi_mode = e->mode;
    }
}

// ---- arbitration + publishing ----

static led_state_t pick_led(uint32_t err, uint32_t warn, bool power)
{
    if (err) {
        return LED_ERROR;
    }
    if (warn) {
        return LED_WARNING;
    }
    switch (s_wifi_mode) {
    case WIFI_MODE_AP_PROVISION: return LED_AP_PROVISION;
    case WIFI_MODE_CONNECTING:   return LED_CONNECTING;
    case WIFI_MODE_CONNECTED:    return power ? LED_CONNECTED_RUNNING : LED_CONNECTED_IDLE;
    default:                     return LED_BOOT;
    }
}

static void poll_button(void)
{
    static int low_count;
    static bool pressed;
    int debounce = BTN_DEBOUNCE_MS / (1000 / MONITOR_HZ);
    if (debounce < 1) {
        debounce = 1;
    }

    if (gpio_get_level(PIN_BTN) == 0) {
        if (low_count < debounce) {
            low_count++;
        }
        if (low_count >= debounce && !pressed) {
            pressed = true;
            ESP_LOGI(TAG, "button -> AP mode");
            wifi_enter_ap();
        }
    } else {
        low_count = 0;
        pressed = false;
    }
}

static void monitor_task(void *arg)
{
    int64_t last = esp_timer_get_time();
    int64_t last_publish = last;
    safety_output_t prev = { .safety_scale = -1.0f };

    const TickType_t period = pdMS_TO_TICKS(1000 / MONITOR_HZ);
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        int64_t now = esp_timer_get_time();
        float dt = (float)(now - last) / 1e6f;
        last = now;

        bool pd_ok;
        float pd_current;
        read_pd(&pd_ok, &pd_current);
        float vin = read_vin();
        bool ntc_valid;
        float laser_temp = read_ntc(&ntc_valid);
        float mcu_temp = read_mcu_temp();
        int fan_rpm = read_fan_rpm(dt);

        float fan_duty = fancontrol_update(ntc_valid ? laser_temp : T_LIMIT_C, dt);

        safety_inputs_t in = {
            .pd_ok = pd_ok,
            .pd_current_a = pd_current,
            .ntc_temp_c = laser_temp,
            .ntc_valid = ntc_valid,
            .fan_rpm = fan_rpm,
            .fan_duty = fan_duty,
            .vin_v = vin,
        };
        safety_output_t out;
        safety_step(&s_safety, &in, LASER_REQUESTED_A, dt, &out);

        if (out.err_flags) {
            laser_emergency_off();
        }

        if (out.safety_scale != prev.safety_scale || out.warn_flags != prev.warn_flags ||
            out.err_flags != prev.err_flags || out.latched != prev.latched) {
            app_safety_evt_t ev = {
                .warn_flags = out.warn_flags,
                .err_flags = out.err_flags,
                .safety_scale = out.safety_scale,
                .latched = out.latched,
            };
            app_bus_post(EVT_SAFETY_CHANGED, &ev, sizeof(ev), 0);
            prev = out;
        }

        app_state_t st;
        appstate_get(&st);
        status_led_set_state(pick_led(out.err_flags, out.warn_flags, st.power));

        poll_button();

        if (now - last_publish >= 1000000) {
            last_publish = now;
            app_metrics_evt_t m = {
                .vin = vin,
                .laser_temp = laser_temp,
                .mcu_temp = mcu_temp,
                .fan_rpm = fan_rpm,
                .fan_duty = fan_duty,
                .pd_current = pd_current,
                .pd_ok = pd_ok,
                .warn_flags = out.warn_flags,
                .err_flags = out.err_flags,
            };
            app_bus_post(EVT_METRICS_UPDATED, &m, sizeof(m), 0);
        }

        vTaskDelayUntil(&wake, period);
    }
}

esp_err_t sysmgr_init(void)
{
    ESP_ERROR_CHECK(status_led_init());
    status_led_set_state(LED_BOOT);

    safety_init(&s_safety);
    app_bus_subscribe(EVT_WIFI_STATE, on_wifi_state, NULL);

    i2c_init();
    adc_init();
    tsens_init();
    pcnt_init();
    ESP_ERROR_CHECK(fancontrol_init());
    button_init();

    return ESP_OK;
}

esp_err_t sysmgr_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(monitor_task, "sysmgr_monitor", 4096, NULL,
                                            6, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "monitor task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
