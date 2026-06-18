// audio.c STUB
#include "audio.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "audio";

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "audio init (stub)");
    return ESP_OK;
}

void audio_get_features(audio_features_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}
