// storage.h

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Open NVS and create the debounced scene-persist timer.
esp_err_t storage_init(void);

// Subscribe for scene changes and apply any persisted scene.
esp_err_t storage_restore_scene(void);

esp_err_t storage_save_wifi(const char *ssid, const char *pass);
bool storage_load_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);

esp_err_t storage_save_pwm_hz(uint32_t hz);
uint32_t storage_load_pwm_hz(void);   // TC_PWM_HZ when unset
void storage_clear_all(void);

#ifdef __cplusplus
}
#endif
