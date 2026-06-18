// truecolors_main.c STUB
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_events.h"
#include "appstate.h"
#include "storage.h"
#include "laser.h"
#include "effects.h"
#include "audio.h"
#include "sysmgr.h"
#include "status_led.h"
#include "wifi_mgr.h"
#include "server.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "TrueColors boot");

    // 1. NVS init + scene staging
    esp_err_t nerr = nvs_flash_init();
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(storage_init());

    // 2. GPIO defaults: lasers held off by EN pulldowns, fan off via R26

    // 3. App event bus + state mutex + state model.
    ESP_ERROR_CHECK(app_bus_init());
    ESP_ERROR_CHECK(appstate_init());

    // 4. Laser (MCPWM, widths 0, generators LOW), effects registry, audio (I2S).
    ESP_ERROR_CHECK(laser_init());
    ESP_ERROR_CHECK(effects_init());
    ESP_ERROR_CHECK(audio_init());

    // 5. System manager + status LED + safety gate.
    ESP_ERROR_CHECK(sysmgr_init());
    ESP_ERROR_CHECK(status_led_init());
    status_led_set_state(LED_BOOT);

    // 6. Wi-Fi (STA -> AP fallback) + mDNS/hostname.
    ESP_ERROR_CHECK(wifi_start());

    // 7. HTTP server (SPA + OTA + /ws) + bus subscribers.
    ESP_ERROR_CHECK(server_start());

    // 8. Prime the scene from persisted state (STORAGE_RESTORE patch).
    ESP_ERROR_CHECK(storage_restore_scene());

    // 9. Start periodic tasks (render / monitor / audio).
    ESP_ERROR_CHECK(effects_start_render());
    ESP_ERROR_CHECK(sysmgr_start());

    ESP_LOGI(TAG, "boot success");
}
