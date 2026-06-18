// effects.c STUB
#include "effects.h"
#include "esp_log.h"

static const char *TAG = "effects";

esp_err_t effects_init(void)
{
    ESP_LOGI(TAG, "effects init (stub)");
    return ESP_OK;
}

esp_err_t effects_start_render(void)
{
    ESP_LOGI(TAG, "effects render task (stub)");
    return ESP_OK;
}
