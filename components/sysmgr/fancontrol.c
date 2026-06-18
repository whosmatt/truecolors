// fancontrol.c STUB
#include "fancontrol.h"
#include "esp_log.h"

static const char *TAG = "fancontrol";

esp_err_t fancontrol_init(void)
{
    ESP_LOGI(TAG, "fancontrol init (stub)");
    return ESP_OK;
}

float fancontrol_update(float temp_c, float dt)
{
    (void)temp_c; (void)dt;
    return 0.0f;
}
