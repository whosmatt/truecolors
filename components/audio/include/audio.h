// audio.h
// I2S PDM RX -> block RMS -> AGC-normalized features snapshot
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Snapshot of audio features, single-writer (audio task) / read per frame.
// Levels are raw per-block AGC-normalized values; effects apply their own
// attack/release shaping.
typedef struct {
    float level;     // 0..1 AGC-normalized broadband level
    float bands[3];  // 0..1 AGC-normalized bass / mid / treble
    float beat;      // 0..1 transient/beat envelope
} audio_features_t;

// Initialize I2S PDM RX and the audio task.
esp_err_t audio_init(void);

// Copy the latest features snapshot (lock-free single-writer).
void audio_get_features(audio_features_t *out);

// Align the coil-whine comb to the laser PWM frequency. Call on PWM change.
void audio_set_notch_hz(uint32_t hz);

#ifdef __cplusplus
}
#endif
