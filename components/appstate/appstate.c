// appstate.c STUB
#include "appstate.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "appstate";

// Default boot scene
static app_state_t s_state = {
    .power = false,
    .brightness = 0.6f,
    .stretch = 0.0f,
    .effect_id = 0,
    .color = {1.0f, 1.0f, 1.0f},
    .speed = 0.5f,
    .audio_sens = 0.5f,
    .seq = 0,
};

esp_err_t appstate_init(void)
{
    ESP_LOGI(TAG, "appstate init (stub)");
    return ESP_OK;
}

void appstate_get(app_state_t *out)
{
    if (out) {
        memcpy(out, &s_state, sizeof(*out));
    }
}
