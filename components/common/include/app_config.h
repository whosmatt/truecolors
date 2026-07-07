// app_config.h

#pragma once

// ---------------------------------------------------------------------------
// Laser PWM / drive (MCPWM)
// ---------------------------------------------------------------------------
#define TC_PWM_HZ            480        // RGB cycle frequency, start 480 Hz (tune)
#define MCPWM_CLK_HZ         10000000   // 10 MHz group clock -> 0.1 us/tick
#define GAP_US               0          // guard gap: settled at 0 (not an H-bridge)
#define MIN_ON_US            8          // LM3409 settle floor, 5-10 us (tune on scope)
#define GAMMA                2.2f       // float gamma exponent (calibratable)

// Derived tick budget (period in MCPWM ticks).
#define TC_PERIOD_TICKS      (MCPWM_CLK_HZ / TC_PWM_HZ)            // 480 Hz -> 20833
#define GAP_TICKS            ((GAP_US * (MCPWM_CLK_HZ / 1000000))) // 0
#define MIN_ON_TICKS         (MIN_ON_US * (MCPWM_CLK_HZ / 1000000))

// ---------------------------------------------------------------------------
// Thermal / fan
// ---------------------------------------------------------------------------
#define FAN_TARGET_C         45.0f      // PID setpoint (laser-module NTC)
#define FAN_PWM_HZ           25000      // 25 kHz, inaudible
#define FAN_PWM_RES_BITS     10         // LEDC resolution
#define FAN_PULSES_PER_REV   2          // confirmed
#define FAN_START_DUTY       0.01f      // any duty spins the fan, already spinning at 0%
// Measured ramp 2026-07-07: ~84 rpm floor below the knee, rpm = 1875*duty - 250
// above it, linear to 1621 rpm at 100%, no hysteresis.
#define FAN_KNEE_DUTY        0.19f      // PID demand maps onto [knee, 1]
#define STALL_DUTY           0.40f      // PID duty above which RPM==0 means stall
#define STALL_DEBOUNCE_S     3.0f       // sustained-stall debounce before latching
// Tuned from logged limit cycle 2026-07-06, rescaled by 1/(1-knee) for the
// knee remap so loop dynamics around equilibrium stay the same.
#define FAN_KP               0.12f
#define FAN_KI               0.0012f

// ---------------------------------------------------------------------------
// Safety thresholds
// ---------------------------------------------------------------------------
#define T_LIMIT_C            55.0f      // overtemp latching fault threshold
#define VIN_NOMINAL_V        20.0f
#define VIN_WARN_V           17.0f      // below this -> "vin_low" warning (no shutdown)
#define LASER_REQUESTED_A    2.5f       // worst-case low-Vrail laser current budget
#define NTC_V_OPEN           3.25f      // above this the NTC reads open
#define NTC_V_SHORT          0.05f      // below this the NTC reads shorted
#define BTN_DEBOUNCE_MS      60

// ---------------------------------------------------------------------------
// Sensing / ADC
// ---------------------------------------------------------------------------
#define ADC_VREF_V           3.3f
#define VIN_DIVIDER_SCALE    7.8f       // (68k + 10k) / 10k
#define ADC_OVERSAMPLE_N     16         // samples averaged per read

// NTC (laser module): 10k @ 25 C, B = 3950.
#define NTC_R_NOMINAL        10000.0f
#define NTC_R_SERIES         10000.0f   // 10k pull-up to 3.3 V
#define NTC_B_VALUE          3950.0f
#define NTC_T0_K             298.15f    // 25 C in kelvin

// ---------------------------------------------------------------------------
// CH224A USB-PD sink (I2C, single-byte transactions only)
// ---------------------------------------------------------------------------
#define CH224A_ADDR_A        0x22       // probe both at init (confirm on hardware)
#define CH224A_ADDR_B        0x23
#define CH224A_I2C_HZ        400000
#define CH224A_REG_STATUS    0x09       // RO: bit3 = PD handshake good
#define CH224A_REG_CURRENT   0x50       // RO: max current, 50 mA/count
#define CH224A_REG_VOLTAGE   0x0A       // WO: gear select (4 = 20 V)
#define CH224A_STATUS_PD_BIT (1 << 3)
#define CH224A_CURRENT_MA_PER_COUNT 50

// ---------------------------------------------------------------------------
// Timing / rates
// ---------------------------------------------------------------------------
#define RENDER_HZ            90         // laser_render task target (60-120 Hz)
#define MONITOR_HZ           10         // sysmgr internal poll rate (5-10 Hz)
#define METRICS_HZ           1          // metrics/telemetry publish rate
#define STATUS_LED_HZ        50         // animation engine tick (smooth fades)

// ---------------------------------------------------------------------------
// Networking / server
// ---------------------------------------------------------------------------
#define WS_MAX_CLIENTS       8          // matches httpd max_open_sockets
#define DEVICE_HOSTNAME      "truecolors"   // -> truecolors.local via mDNS

// ---------------------------------------------------------------------------
// Persistence (NVS)
// ---------------------------------------------------------------------------
#define NVS_NAMESPACE        "truecolors"
#define NVS_KEY_SCENE        "scene"        // packed app_state blob
#define NVS_KEY_WIFI_SSID    "wifi_ssid"
#define NVS_KEY_WIFI_PASS    "wifi_pass"
#define NVS_KEY_HOSTNAME     "hostname"
#define PERSIST_DEBOUNCE_MS  2000           // coalesce scene writes to flash
