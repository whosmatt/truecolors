// laser.c

#include "laser.h"
#include "esp_log.h"

static const char *TAG = "laser";

esp_err_t laser_init(void)
{
    ESP_LOGI(TAG, "laser init (stub)");
    return ESP_OK;
}

void laser_set(float r, float g, float b, float stretch)
{
    (void)r; (void)g; (void)b; (void)stretch;
}

void laser_emergency_off(void)
{
}
