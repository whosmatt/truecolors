// status_led.h STUB

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_BOOT = 0,            // white  · slow breathe
    LED_AP_PROVISION,        // blue   · breathe (~1 Hz)
    LED_CONNECTING,          // blue   · faster breathe
    LED_CONNECTED_IDLE,      // green  · dim solid (power off)
    LED_CONNECTED_RUNNING,   // green  · gentle breathe
    LED_WARNING,             // amber  · slow pulse (on -> fade)
    LED_ERROR,               // red    · pulse (sharp on -> fade, repeating)
    LED_STATE_COUNT,
} led_state_t;

// Init the led_strip and start the animation task.
esp_err_t status_led_init(void);

// Set the currently displayed state. Animation restarts cleanly on change.
void status_led_set_state(led_state_t state);

#ifdef __cplusplus
}
#endif
