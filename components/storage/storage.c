// storage.c
#include "storage.h"

#include <string.h>
#include "appstate.h"
#include "app_config.h"
#include "app_events.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "storage";

#define SCENE_BLOB_VERSION 1

typedef struct {
    uint8_t  version;
    bool     power;
    float    brightness;
    float    stretch;
    uint16_t effect_id;
    float    color[3];
    float    speed;
    float    audio_sens;
    float    params[EFFECT_PARAM_MAX];
} scene_blob_t;

static nvs_handle_t s_nvs;
static esp_timer_handle_t s_persist_timer;

static void persist_now(void *arg)
{
    app_state_t st;
    appstate_get(&st);

    scene_blob_t blob = {
        .version = SCENE_BLOB_VERSION,
        .power = st.power,
        .brightness = st.brightness,
        .stretch = st.stretch,
        .effect_id = st.effect_id,
        .speed = st.speed,
        .audio_sens = st.audio_sens,
    };
    memcpy(blob.color, st.color, sizeof(blob.color));
    memcpy(blob.params, st.params, sizeof(blob.params));

    esp_err_t err = nvs_set_blob(s_nvs, NVS_KEY_SCENE, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scene persist failed: %s", esp_err_to_name(err));
    }
}

static void on_state_changed(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const app_state_changed_evt_t *evt = data;
    if (evt && evt->src == APP_SRC_STORAGE_RESTORE) {
        return;
    }
    esp_timer_stop(s_persist_timer);
    esp_timer_start_once(s_persist_timer, (uint64_t)PERSIST_DEBOUNCE_MS * 1000);
}

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_timer_create_args_t targs = {
        .callback = persist_now,
        .name = "scene_persist",
    };
    return esp_timer_create(&targs, &s_persist_timer);
}

esp_err_t storage_restore_scene(void)
{
    esp_err_t err = app_bus_subscribe(EVT_STATE_CHANGED, on_state_changed, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "subscribe failed: %s", esp_err_to_name(err));
    }

    scene_blob_t blob;
    size_t len = sizeof(blob);
    err = nvs_get_blob(s_nvs, NVS_KEY_SCENE, &blob, &len);
    if (err != ESP_OK || len != sizeof(blob) || blob.version != SCENE_BLOB_VERSION) {
        ESP_LOGI(TAG, "no saved scene, using defaults");
        return ESP_OK;
    }

    app_patch_t patch = {
        .mask = STATE_F_POWER | STATE_F_BRIGHTNESS | STATE_F_STRETCH |
                STATE_F_EFFECT | STATE_F_COLOR | STATE_F_SPEED |
                STATE_F_AUDIO_SENS | STATE_F_PARAMS,
        .power = blob.power,
        .brightness = blob.brightness,
        .stretch = blob.stretch,
        .effect_id = blob.effect_id,
        .speed = blob.speed,
        .audio_sens = blob.audio_sens,
    };
    memcpy(patch.color, blob.color, sizeof(patch.color));
    memcpy(patch.params, blob.params, sizeof(patch.params));

    return appstate_apply_patch(APP_SRC_STORAGE_RESTORE, &patch);
}
