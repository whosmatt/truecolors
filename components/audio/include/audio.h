// audio.h
// TODO I2S PDM RX -> ring buffer -> features snapshot for audio-reactive effects
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Snapshot of audio features, single-writer (audio task) / read per frame.
typedef struct {
    float level;   // 0..1 broadband level
    float beat;    // 0..1 transient/beat envelope
} audio_features_t;

// Initialize I2S PDM RX and the audio task.
esp_err_t audio_init(void);

// Copy the latest features snapshot (lock-free single-writer).
void audio_get_features(audio_features_t *out);

#ifdef __cplusplus
}
#endif
