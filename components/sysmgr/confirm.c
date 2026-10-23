// confirm.c
#include "confirm.h"

#include <stdint.h>
#include <stdbool.h>
#include "sysmgr.h"
#include "app_config.h"
#include "app_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

// Brief extra debounce after confirmation press to avoid accidentally triggering AP mode
#define CONFIRM_GRACE_US 3000000

// Ensure ARMED event happens before any outcome
static SemaphoreHandle_t s_lock;
static volatile bool s_pending;
static int64_t s_deadline_us;
static int64_t s_ended_us = INT64_MIN;

static void post(confirm_state_t state, uint32_t remaining_ms)
{
    app_confirm_evt_t e = { .state = state, .remaining_ms = remaining_ms };
    app_bus_post(EVT_CONFIRM, &e, sizeof(e), pdMS_TO_TICKS(100));
}

void confirm_init(void)
{
    s_lock = xSemaphoreCreateMutex();
}

bool confirm_pending(void)
{
    return s_pending;
}

void sysmgr_confirm_arm(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_pending = true;
    s_deadline_us = esp_timer_get_time() + (int64_t)CONFIRM_TIMEOUT_MS * 1000;
    post(CONFIRM_ARMED, CONFIRM_TIMEOUT_MS);
    xSemaphoreGive(s_lock);
}

void sysmgr_confirm_cancel(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_pending) {
        s_pending = false;
        s_ended_us = esp_timer_get_time();
        post(CONFIRM_CANCELLED, 0);
    }
    xSemaphoreGive(s_lock);
}

bool confirm_poll(int64_t now_us, bool pressed)
{
    bool consumed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_pending) {
        if (pressed) {
            post(CONFIRM_CONFIRMED, 0);
            s_pending = false;
            s_ended_us = now_us;
            consumed = true;
        } else if (now_us >= s_deadline_us) {
            post(CONFIRM_EXPIRED, 0);
            s_pending = false;
            s_ended_us = now_us;
        }
    } else if (pressed && now_us - s_ended_us < CONFIRM_GRACE_US) {
        consumed = true;
    }
    xSemaphoreGive(s_lock);
    return consumed;
}
