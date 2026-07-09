// app_events.h

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

// Custom event base for everything on the app bus.
ESP_EVENT_DECLARE_BASE(APP_EVENT);

// Event IDs on APP_EVENT.
typedef enum {
    EVT_STATE_CHANGED = 1,   // appstate -> server, effects, storage
    EVT_METRICS_UPDATED,     // sysmgr (1 Hz) -> server, fancontrol, status LED
    EVT_SAFETY_CHANGED,      // sysmgr -> server, effects/laser, status LED
    EVT_WIFI_STATE,          // wifi -> server, status LED, mDNS
} app_event_id_t;

// Origin of a state change (carried in EVT_STATE_CHANGED).
typedef enum {
    APP_SRC_WS_CLIENT = 0,   // a WebSocket command
    APP_SRC_STORAGE_RESTORE, // boot restore from NVS
    APP_SRC_INTERNAL,        // firmware-initiated change
} app_src_t;

// Warning / error flag bits (shared by safety + metrics payloads and the WS
// "warn"/"err" string arrays). Warnings never cut lasers; errors gate them.
typedef enum {
    FLAG_VIN_LOW     = (1 << 0),  // "vin_low"      (warning)
    FLAG_UNDERCURRENT= (1 << 1),  // "undercurrent" (warning/limit)
    FLAG_PD_LOST     = (1 << 2),  // "pd_lost"      (warning)
    FLAG_OVERTEMP    = (1 << 3),  // "overtemp"     (error, latching)
    FLAG_FAN_STALL   = (1 << 4),  // "fan_stall"    (error, latching)
} app_flag_t;

// EVT_STATE_CHANGED payload. Subscribers re-read appstate under its lock; the
// payload just says "what changed, in what order, from where".
typedef struct {
    uint32_t seq;            // monotonic state version after this change
    uint32_t changed_mask;   // bitmask of fields touched (see appstate.h)
    app_src_t src;
    uint32_t origin;         // originating client's echo tag, 0 = none
} app_state_changed_evt_t;

// EVT_METRICS_UPDATED payload (1 Hz). Physical units; never persisted.
typedef struct {
    float vin;               // V, measured rail
    float laser_temp;        // C, module NTC
    float mcu_temp;          // C, on-die sensor
    int   fan_rpm;
    float fan_duty;          // 0..1
    float pd_current;        // A, CH224A 0x50 budget
    bool  pd_ok;             // CH224A 0x09 bit3
    float audio_db;          // dB SPL, slow-averaged (mic datasheet calibrated)
    float bpm;               // tempo tracker, 0 while unlocked
    uint32_t warn_flags;     // app_flag_t bits
    uint32_t err_flags;
} app_metrics_evt_t;

// EVT_SAFETY_CHANGED payload (on transition only).
typedef struct {
    uint32_t warn_flags;
    uint32_t err_flags;
    float safety_scale;      // 0..1 multiplier applied before laser_set
    bool  latched;           // a latching fault is active (clears only on reboot)
} app_safety_evt_t;

// Wi-Fi connectivity mode (carried in EVT_WIFI_STATE).
typedef enum {
    WIFI_MODE_BOOT = 0,
    WIFI_MODE_CONNECTING,
    WIFI_MODE_CONNECTED,
    WIFI_MODE_AP_PROVISION,
} app_wifi_mode_t;

// EVT_WIFI_STATE payload.
typedef struct {
    app_wifi_mode_t mode;
    char ssid[33];
    char ip[16];
    char hostname[32];
} app_wifi_evt_t;

// ---------------------------------------------------------------------------
// app_bus API (implemented in app_bus.c)
// ---------------------------------------------------------------------------

// Create the dedicated event loop + its task. Call once, early in app_main.
esp_err_t app_bus_init(void);

// Post an event onto the app bus. `data`/`size` are copied by esp_event.
esp_err_t app_bus_post(app_event_id_t id, const void *data, size_t size,
                       TickType_t ticks_to_wait);

// Subscribe a handler to one event id (ESP_EVENT_ANY_ID also allowed via id).
esp_err_t app_bus_subscribe(app_event_id_t id, esp_event_handler_t handler,
                            void *arg);

// Underlying loop handle (for advanced use / unregister).
esp_event_loop_handle_t app_bus_loop(void);

#ifdef __cplusplus
}
#endif
