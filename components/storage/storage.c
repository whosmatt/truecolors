// storage.c STUB

#include "storage.h"
#include "esp_log.h"

static const char *TAG = "storage";

esp_err_t storage_init(void)
{
    ESP_LOGI(TAG, "storage init (stub)");
    return ESP_OK;
}

esp_err_t storage_restore_scene(void)
{
    ESP_LOGI(TAG, "storage restore scene (stub)");
    return ESP_OK;
}
