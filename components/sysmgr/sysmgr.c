// sysmgr.c STUB
#include "sysmgr.h"
#include "esp_log.h"

static const char *TAG = "sysmgr";

esp_err_t sysmgr_init(void)
{
    ESP_LOGI(TAG, "sysmgr init (stub)");
    return ESP_OK;
}

esp_err_t sysmgr_start(void)
{
    ESP_LOGI(TAG, "sysmgr monitor task (stub)");
    return ESP_OK;
}
