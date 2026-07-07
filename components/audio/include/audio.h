// audio.h
// I2S PDM RX -> block RMS -> AGC-normalized features snapshot
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Snapshot of audio features, single-writer (audio task) / read per frame.
typedef struct {
    float level;     // 0..1 AGC-normalized broadband level (== bands[1])
    float bands[3];  // 0..1 AGC-normalized bass / broadband / treble
    float beat;      // 0..1 transient/beat envelope
} audio_features_t;

// Initialize I2S PDM RX and the audio task.
esp_err_t audio_init(void);

// Copy the latest features snapshot (lock-free single-writer).
void audio_get_features(audio_features_t *out);

#ifdef __cplusplus
}
#endif
