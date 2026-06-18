// audio.c STUB
#include "audio.h"
#include <math.h>
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "audio";

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "audio init (stub)");
    return ESP_OK;
}

void audio_get_features(audio_features_t *out)
{
    if (!out) {
        return;
    }
    // Placeholder LFO until real PDM DSP lands.
    float t = (float)esp_timer_get_time() / 1e6f;
    out->level = 0.5f * (1.0f - cosf(6.28318530718f * 0.2f * t));
    out->beat = 0.0f;
}
