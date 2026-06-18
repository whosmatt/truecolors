// board_pins.h

#pragma once

#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// GPIO map (ESP32-S3-WROOM-1 N8R8)
// ---------------------------------------------------------------------------
// Laser enables — active-high to the LM3409 EN/PWM input (high = laser on).
// Each net has a 10k pulldown so lasers stay off while the GPIO is Hi-Z.
#define PIN_EN_R        GPIO_NUM_40   // MCPWM operator 0 -> driverR
#define PIN_EN_G        GPIO_NUM_39   // MCPWM operator 1 -> driverG
#define PIN_EN_B        GPIO_NUM_38   // MCPWM operator 2 -> driverB

// Fan (5 V 4-wire PC fan via low-side BSS138).
#define PIN_FAN_PWM     GPIO_NUM_6    // LEDC, 25 kHz, output_invert=1
#define PIN_FAN_TACH    GPIO_NUM_7    // PCNT, open-collector, 10k pull-up to 3.3 V

// CH224A USB-PD sink (I2C master).
#define PIN_I2C_SDA     GPIO_NUM_8    // 2.2k pull-up to 3.3 V
#define PIN_I2C_SCL     GPIO_NUM_9    // 2.2k pull-up to 3.3 V

// Analog sense (ADC1 only — ADC2 is unusable while Wi-Fi is active).
#define PIN_VIN_ADC     GPIO_NUM_4    // ADC1_CH3, 20 V rail via 68k/10k divider
#define PIN_NTC_ADC     GPIO_NUM_10   // ADC1_CH9, laser-module NTC via 10k/NTC divider

// PDM MEMS microphone (MSM261DGT003).
#define PIN_MIC_CLK     GPIO_NUM_12   // I2S PDM CLK
#define PIN_MIC_DATA    GPIO_NUM_13   // I2S PDM DATA (RX)

// WS2812B-B status LED.
#define PIN_LED_DATA    GPIO_NUM_11   // RMT TX (led_strip)

// User button / boot strap (SW1 + 10k pull-up to 3.3 V).
// Boot strap at reset; free to read as a button once running.
#define PIN_BTN         GPIO_NUM_0

// ---------------------------------------------------------------------------
// Peripheral-unit allocation
// ---------------------------------------------------------------------------
// MCPWM — one group, one timer, three operators (one pulse per laser channel).
#define MCPWM_GROUP_ID      0
#define MCPWM_TIMER_ID      0
#define MCPWM_OPER_R        0
#define MCPWM_OPER_G        1
#define MCPWM_OPER_B        2

// LEDC — fan PWM.
#define LEDC_FAN_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_FAN_TIMER      LEDC_TIMER_0
#define LEDC_FAN_CHANNEL    LEDC_CHANNEL_0

// PCNT — fan tach.
#define PCNT_FAN_UNIT_ID    0   // pcnt_new_unit() handle is created in fancontrol

// I2C — CH224A bus.
#define I2C_PORT_CH224A     0

// I2S — PDM RX microphone.
#define I2S_PORT_MIC        0

// ADC — both sense channels are on ADC1.
#define ADC_UNIT_SENSE      ADC_UNIT_1
#define ADC_CH_VIN          ADC_CHANNEL_3   // GPIO4
#define ADC_CH_NTC          ADC_CHANNEL_9   // GPIO10
