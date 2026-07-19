// ws.c
#include "ws_internal.h"
#include "appstate.h"
#include "effects.h"
#include "wifi_mgr.h"
#include "storage.h"
#include "laser.h"
#include "audio.h"
#include "app_config.h"
#include "app_events.h"

#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "ws";

#define ALL_FIELDS (STATE_F_POWER | STATE_F_BRIGHTNESS | STATE_F_STRETCH | \
                    STATE_F_EFFECT | STATE_F_COLOR | STATE_F_SPEED |       \
                    STATE_F_AUDIO_SENS | STATE_F_PARAMS)

static httpd_handle_t s_server;
static int s_fds[WS_MAX_CLIENTS];
static int s_fd_count;
static SemaphoreHandle_t s_lock;
static app_metrics_evt_t s_last_metrics;

// ---- client table ----

static void add_client(int fd)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_fd_count; i++) {
        if (s_fds[i] == fd) {
            xSemaphoreGive(s_lock);
            return;
        }
    }
    if (s_fd_count < WS_MAX_CLIENTS) {
        s_fds[s_fd_count++] = fd;
    }
    xSemaphoreGive(s_lock);
}

void ws_on_close(int fd)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_fd_count; i++) {
        if (s_fds[i] == fd) {
            s_fds[i] = s_fds[--s_fd_count];
            break;
        }
    }
    xSemaphoreGive(s_lock);
}

// ---- async send / broadcast ----

typedef struct {
    httpd_handle_t hd;
    int fd;
    char *json;
} async_ctx_t;

static void async_send(void *arg)
{
    async_ctx_t *c = arg;
    httpd_ws_frame_t f = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)c->json,
        .len = strlen(c->json),
    };
    if (httpd_ws_send_frame_async(c->hd, c->fd, &f) != ESP_OK) {
        ws_on_close(c->fd);
    }
    free(c->json);
    free(c);
}

static void broadcast(const char *json)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_fd_count; i++) {
        async_ctx_t *c = malloc(sizeof(*c));
        if (!c) {
            continue;
        }
        c->hd = s_server;
        c->fd = s_fds[i];
        c->json = strdup(json);
        esp_err_t qerr = ESP_OK;
        if (!c->json || (qerr = httpd_queue_work(s_server, async_send, c)) != ESP_OK) {
            if (c->json) {
                ESP_LOGW(TAG, "httpd_queue_work fd=%d failed: %s", c->fd,
                         esp_err_to_name(qerr));
            }
            free(c->json);
            free(c);
        }
    }
    xSemaphoreGive(s_lock);
}

static void send_one(httpd_req_t *req, const char *json)
{
    httpd_ws_frame_t f = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };
    httpd_ws_send_frame(req, &f);
}

// ---- JSON builders ----

static const char *mode_str(app_wifi_mode_t m)
{
    switch (m) {
    case WIFI_MODE_CONNECTING:   return "connecting";
    case WIFI_MODE_CONNECTED:    return "sta";
    case WIFI_MODE_AP_PROVISION: return "ap";
    default:                     return "boot";
    }
}

static void add_scene_fields(cJSON *o, const app_state_t *st, uint32_t mask, size_t np)
{
    if (mask & STATE_F_POWER)      cJSON_AddBoolToObject(o, "power", st->power);
    if (mask & STATE_F_BRIGHTNESS) cJSON_AddNumberToObject(o, "brightness", st->brightness);
    if (mask & STATE_F_STRETCH)    cJSON_AddNumberToObject(o, "stretch", st->stretch);
    if (mask & STATE_F_SPEED)      cJSON_AddNumberToObject(o, "speed", st->speed);
    if (mask & STATE_F_AUDIO_SENS) cJSON_AddNumberToObject(o, "audioSens", st->audio_sens);
    if (mask & STATE_F_EFFECT)     cJSON_AddNumberToObject(o, "effectId", st->effect_id);
    if (mask & STATE_F_COLOR) {
        cJSON_AddNumberToObject(o, "r", st->color[0]);
        cJSON_AddNumberToObject(o, "g", st->color[1]);
        cJSON_AddNumberToObject(o, "b", st->color[2]);
    }
    if (mask & STATE_F_PARAMS) {
        cJSON *p = cJSON_AddArrayToObject(o, "params");
        for (size_t i = 0; i < np; i++) {
            cJSON_AddItemToArray(p, cJSON_CreateNumber(st->params[i]));
        }
    }
}

static cJSON *net_obj(void)
{
    app_wifi_evt_t net;
    wifi_get_state(&net);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", mode_str(net.mode));
    cJSON_AddStringToObject(o, "ssid", net.ssid);
    cJSON_AddStringToObject(o, "hostname", net.hostname);
    cJSON_AddStringToObject(o, "ip", net.ip);
    return o;
}

static size_t effect_nparams(uint16_t id)
{
    size_t n;
    const effect_desc_t *cat = effects_get_catalog(&n);
    return id < n ? cat[id].n_params : 0;
}

static char *build_snapshot(void)
{
    app_state_t st;
    appstate_get(&st);
    size_t ne;
    const effect_desc_t *cat = effects_get_catalog(&ne);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "snapshot");
    cJSON_AddNumberToObject(root, "seq", st.seq);

    cJSON *scene = cJSON_AddObjectToObject(root, "scene");
    add_scene_fields(scene, &st, ALL_FIELDS, effect_nparams(st.effect_id));

    cJSON *effs = cJSON_AddArrayToObject(root, "effects");
    for (size_t i = 0; i < ne; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "id", cat[i].id);
        cJSON_AddStringToObject(e, "name", cat[i].display_name);
        cJSON_AddNumberToObject(e, "globals", cat[i].global_mask);
        if (cat[i].epilepsy_unsafe) {
            cJSON_AddBoolToObject(e, "epilepsyUnsafe", true);
        }
        if (cat[i].speed_unit) {
            cJSON_AddStringToObject(e, "speedUnit", cat[i].speed_unit);
            cJSON_AddNumberToObject(e, "speedScale",
                                    cat[i].speed_scale != 0.0f ? cat[i].speed_scale : 1.0f);
        }
        cJSON *ps = cJSON_AddArrayToObject(e, "params");
        for (size_t j = 0; j < cat[i].n_params; j++) {
            const effect_param_def_t *d = &cat[i].params[j];
            cJSON *pd = cJSON_CreateObject();
            cJSON_AddStringToObject(pd, "name", d->name);
            cJSON_AddNumberToObject(pd, "min", d->min);
            cJSON_AddNumberToObject(pd, "max", d->max);
            cJSON_AddNumberToObject(pd, "def", d->def);
            if (d->safe_min > d->min) {
                cJSON_AddNumberToObject(pd, "safeMin", d->safe_min);
            }
            if (d->safe_max != 0.0f) {
                cJSON_AddNumberToObject(pd, "safeMax", d->safe_max);
            }
            if (d->unit) {
                cJSON_AddStringToObject(pd, "unit", d->unit);
            }
            if (d->unit_scale != 0.0f) {
                cJSON_AddNumberToObject(pd, "scale", d->unit_scale);
            }
            if (d->unit_offset != 0.0f) {
                cJSON_AddNumberToObject(pd, "offset", d->unit_offset);
            }
            if (d->labels) {
                cJSON *ls = cJSON_AddArrayToObject(pd, "labels");
                for (const char *const *l = d->labels; *l; l++) {
                    cJSON_AddItemToArray(ls, cJSON_CreateString(*l));
                }
            }
            cJSON_AddItemToArray(ps, pd);
        }
        cJSON_AddItemToArray(effs, e);
    }

    cJSON_AddItemToObject(root, "net", net_obj());
    cJSON_AddNumberToObject(root, "pwmHz", laser_get_pwm_hz());
    cJSON_AddBoolToObject(root, "epilepsySafe", effects_get_epilepsy_safe());

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_pwm_hz(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "pwm_hz");
    cJSON_AddNumberToObject(root, "hz", laser_get_pwm_hz());
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_beatgrid(const app_beatgrid_evt_t *e)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "beatgrid");
    cJSON_AddNumberToObject(root, "t", e->t);
    cJSON_AddNumberToObject(root, "blockHz", e->block_hz);
    cJSON_AddNumberToObject(root, "phase", e->phase);
    cJSON_AddNumberToObject(root, "period", e->period);
    cJSON_AddNumberToObject(root, "bpm", e->bpm);
    cJSON_AddBoolToObject(root, "kick", e->kick);
    cJSON_AddBoolToObject(root, "snare", e->snare);
    cJSON_AddBoolToObject(root, "met", e->met);
    cJSON_AddNumberToObject(root, "off", e->off);
    cJSON_AddNumberToObject(root, "nudge", e->nudge);
    cJSON_AddNumberToObject(root, "err", e->err);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_epilepsy_safe(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "epilepsy_safe");
    cJSON_AddBoolToObject(root, "on", effects_get_epilepsy_safe());
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_patch(uint32_t mask, uint32_t seq, app_src_t src, uint32_t origin)
{
    app_state_t st;
    appstate_get(&st);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "patch");
    cJSON_AddNumberToObject(root, "seq", seq);
    cJSON_AddNumberToObject(root, "src", src);
    cJSON_AddNumberToObject(root, "origin", origin);
    cJSON *set = cJSON_AddObjectToObject(root, "set");
    add_scene_fields(set, &st, mask, effect_nparams(st.effect_id));
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static void add_flag_strings(cJSON *arr, uint32_t flags)
{
    if (flags & FLAG_VIN_LOW)      cJSON_AddItemToArray(arr, cJSON_CreateString("vin_low"));
    if (flags & FLAG_UNDERCURRENT) cJSON_AddItemToArray(arr, cJSON_CreateString("undercurrent"));
    if (flags & FLAG_PD_LOST)      cJSON_AddItemToArray(arr, cJSON_CreateString("pd_lost"));
    if (flags & FLAG_OVERTEMP)     cJSON_AddItemToArray(arr, cJSON_CreateString("overtemp"));
    if (flags & FLAG_FAN_STALL)    cJSON_AddItemToArray(arr, cJSON_CreateString("fan_stall"));
}

static char *build_metrics(const app_metrics_evt_t *m)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "metrics");
    cJSON_AddNumberToObject(root, "vin", m->vin);
    cJSON_AddNumberToObject(root, "laserTemp", m->laser_temp);
    cJSON_AddNumberToObject(root, "mcuTemp", m->mcu_temp);
    cJSON_AddNumberToObject(root, "fanRpm", m->fan_rpm);
    cJSON_AddNumberToObject(root, "fanDuty", m->fan_duty);
    cJSON_AddNumberToObject(root, "pdCurrent", m->pd_current);
    cJSON_AddBoolToObject(root, "pdOk", m->pd_ok);
    cJSON_AddNumberToObject(root, "audioDb", m->audio_db);
    cJSON_AddNumberToObject(root, "bpm", m->bpm);
    add_flag_strings(cJSON_AddArrayToObject(root, "warn"), m->warn_flags);
    add_flag_strings(cJSON_AddArrayToObject(root, "err"), m->err_flags);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

// ---- inbound message handling ----

static void apply_set(cJSON *set, uint32_t origin)
{
    app_patch_t p = { .origin = origin };
    cJSON *v;

    if ((v = cJSON_GetObjectItem(set, "power"))) { p.mask |= STATE_F_POWER; p.power = cJSON_IsTrue(v); }
    if ((v = cJSON_GetObjectItem(set, "brightness")) && cJSON_IsNumber(v)) { p.mask |= STATE_F_BRIGHTNESS; p.brightness = v->valuedouble; }
    if ((v = cJSON_GetObjectItem(set, "stretch")) && cJSON_IsNumber(v)) { p.mask |= STATE_F_STRETCH; p.stretch = v->valuedouble; }
    if ((v = cJSON_GetObjectItem(set, "speed")) && cJSON_IsNumber(v)) { p.mask |= STATE_F_SPEED; p.speed = v->valuedouble; }
    if ((v = cJSON_GetObjectItem(set, "audioSens")) && cJSON_IsNumber(v)) { p.mask |= STATE_F_AUDIO_SENS; p.audio_sens = v->valuedouble; }
    if ((v = cJSON_GetObjectItem(set, "effectId")) && cJSON_IsNumber(v)) { p.mask |= STATE_F_EFFECT; p.effect_id = (uint16_t)v->valueint; }

    cJSON *r = cJSON_GetObjectItem(set, "r");
    cJSON *g = cJSON_GetObjectItem(set, "g");
    cJSON *b = cJSON_GetObjectItem(set, "b");
    if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
        p.mask |= STATE_F_COLOR;
        p.color[0] = r->valuedouble;
        p.color[1] = g->valuedouble;
        p.color[2] = b->valuedouble;
    }

    cJSON *params = cJSON_GetObjectItem(set, "params");
    if (cJSON_IsArray(params)) {
        p.mask |= STATE_F_PARAMS;
        int n = cJSON_GetArraySize(params);
        for (int i = 0; i < n && i < EFFECT_PARAM_MAX; i++) {
            p.params[i] = cJSON_GetArrayItem(params, i)->valuedouble;
        }
    }

    if (p.mask) {
        appstate_apply_patch(APP_SRC_WS_CLIENT, &p);
    }
}

static void send_wifi_list(httpd_req_t *req)
{
    wifi_ap_info_t aps[16];
    int n = wifi_scan(aps, 16);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "wifi_list");
    cJSON *arr = cJSON_AddArrayToObject(root, "aps");
    for (int i = 0; i < n; i++) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(a, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(a, "auth", aps[i].auth);
        cJSON_AddItemToArray(arr, a);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out) {
        send_one(req, out);
        free(out);
    }
}

static void handle_message(httpd_req_t *req, const char *data)
{
    cJSON *root = cJSON_Parse(data);
    if (!root) {
        return;
    }
    const cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type)) {
        const char *t = type->valuestring;
        if (strcmp(t, "patch") == 0) {
            cJSON *set = cJSON_GetObjectItem(root, "set");
            cJSON *cid = cJSON_GetObjectItem(root, "cid");
            if (set) {
                apply_set(set, cJSON_IsNumber(cid) ? (uint32_t)cid->valuedouble : 0);
            }
        } else if (strcmp(t, "get_snapshot") == 0) {
            char *snap = build_snapshot();
            if (snap) { send_one(req, snap); free(snap); }
        } else if (strcmp(t, "wifi_scan") == 0) {
            send_wifi_list(req);
        } else if (strcmp(t, "wifi_provision") == 0) {
            cJSON *s = cJSON_GetObjectItem(root, "ssid");
            cJSON *p = cJSON_GetObjectItem(root, "pass");
            if (cJSON_IsString(s)) {
                wifi_provision(s->valuestring, cJSON_IsString(p) ? p->valuestring : "");
            }
        } else if (strcmp(t, "wifi_ap") == 0) {
            wifi_enter_ap();
        } else if (strcmp(t, "reboot") == 0) {
            esp_restart();
        } else if (strcmp(t, "factory_reset") == 0) {
            storage_clear_all();
            esp_restart();
        } else if (strcmp(t, "set_pwm_hz") == 0) {
            cJSON *hz = cJSON_GetObjectItem(root, "hz");
            if (cJSON_IsNumber(hz) &&
                laser_set_pwm_hz((uint32_t)hz->valuedouble) == ESP_OK) {
                storage_save_pwm_hz((uint32_t)hz->valuedouble);
                audio_set_notch_hz(laser_get_pwm_hz());
                char *json = build_pwm_hz();
                if (json) { broadcast(json); free(json); }
            }
        } else if (strcmp(t, "set_epilepsy_safe") == 0) {
            cJSON *on = cJSON_GetObjectItem(root, "on");
            if (cJSON_IsBool(on)) {
                effects_set_epilepsy_safe(cJSON_IsTrue(on));
                storage_save_epilepsy_safe(cJSON_IsTrue(on));
                char *json = build_epilepsy_safe();
                if (json) { broadcast(json); free(json); }
            }
        }
    }
    cJSON_Delete(root);
}

// ---- /ws handler ----

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        add_client(fd);
        char *snap = build_snapshot();
        if (snap) {
            send_one(req, snap);
            free(snap);
        }
        return ESP_OK;
    }

    // Track the client here too (idempotent): the GET-handshake branch above is
    // the intended add point, but every client sends get_snapshot right after
    // open, so adding on the first data frame guarantees it lands in s_fds[]
    // even if the handshake add was missed — otherwise broadcasts (live patches
    // + 1 Hz metrics) silently reach no one.
    add_client(httpd_req_to_sockfd(req));

    httpd_ws_frame_t frame = { .type = HTTPD_WS_TYPE_TEXT };
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    if (frame.len == 0 || frame.len > 1024) {
        return ESP_OK;
    }
    uint8_t *buf = calloc(1, frame.len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    frame.payload = buf;
    if (httpd_ws_recv_frame(req, &frame, frame.len) == ESP_OK) {
        handle_message(req, (char *)buf);
    }
    free(buf);
    return ESP_OK;
}

// ---- bus subscribers ----

static void on_state_changed(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const app_state_changed_evt_t *e = data;
    char *json = build_patch(e->changed_mask, e->seq, e->src, e->origin);
    if (json) { broadcast(json); free(json); }
}

static void on_metrics(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    s_last_metrics = *(const app_metrics_evt_t *)data;
    char *json = build_metrics(&s_last_metrics);
    if (json) { broadcast(json); free(json); }
}

static void on_safety(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const app_safety_evt_t *e = data;
    s_last_metrics.warn_flags = e->warn_flags;
    s_last_metrics.err_flags = e->err_flags;
    char *json = build_metrics(&s_last_metrics);
    if (json) { broadcast(json); free(json); }
}

static void on_beatgrid(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (s_fd_count == 0) {
        return;   // event-rate traffic: skip the JSON build with no clients
    }
    char *json = build_beatgrid((const app_beatgrid_evt_t *)data);
    if (json) { broadcast(json); free(json); }
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    cJSON *root = net_obj();
    cJSON_AddStringToObject(root, "type", "net");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json) { broadcast(json); free(json); }
}

void ws_start(httpd_handle_t server)
{
    s_server = server;
    s_lock = xSemaphoreCreateMutex();

    static const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_uri);

    app_bus_subscribe(EVT_STATE_CHANGED, on_state_changed, NULL);
    app_bus_subscribe(EVT_METRICS_UPDATED, on_metrics, NULL);
    app_bus_subscribe(EVT_SAFETY_CHANGED, on_safety, NULL);
    app_bus_subscribe(EVT_WIFI_STATE, on_wifi, NULL);
    app_bus_subscribe(EVT_BEATGRID, on_beatgrid, NULL);
}
