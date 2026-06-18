// app_bus.c
#include "app_events.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static const char *TAG = "app_bus";

static esp_event_loop_handle_t s_loop;

esp_err_t app_bus_init(void)
{
    if (s_loop) {
        return ESP_OK;  // idempotent
    }

    // Dedicated loop + task, deliberately separate from the default system loop
    // so wifi/netif event storms can't delay scene/metrics delivery.
    const esp_event_loop_args_t args = {
        .queue_size = 32,
        .task_name = "app_event_loop",
        .task_priority = 8,            // above idle, below render
        .task_stack_size = 4096,
        .task_core_id = 0,             // co-located with wifi/httpd on core 0
    };

    esp_err_t err = esp_event_loop_create(&args, &s_loop);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "app bus ready");
    return ESP_OK;
}

esp_err_t app_bus_post(app_event_id_t id, const void *data, size_t size,
                       TickType_t ticks_to_wait)
{
    if (!s_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_post_to(s_loop, APP_EVENT, id, data, size, ticks_to_wait);
}

esp_err_t app_bus_subscribe(app_event_id_t id, esp_event_handler_t handler,
                            void *arg)
{
    if (!s_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_handler_instance_register_with(
        s_loop, APP_EVENT, id, handler, arg, NULL);
}

esp_event_loop_handle_t app_bus_loop(void)
{
    return s_loop;
}
