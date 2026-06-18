// laser.c

#include "laser.h"
#include "esp_log.h"

static const char *TAG = "laser";

esp_err_t laser_init(void)
{
    ESP_LOGI(TAG, "laser init (stub) — generators held LOW");
    return ESP_OK;
}

void laser_set(float r, float g, float b, float stretch)
{
    (void)r; (void)g; (void)b; (void)stretch;  // no-op until step 3
}

void laser_emergency_off(void)
{
    // no-op until step 3 (EN pulldowns hold lasers off meanwhile)
}
