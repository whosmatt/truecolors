// audio.h
// I2S PDM RX -> block RMS -> AGC-normalized features snapshot
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "beat_types.h"
#include "frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

// Snapshot of audio features, single-writer (audio task) / read per frame.
// Levels are raw per-block AGC-normalized values; effects apply their own
// attack/release shaping.
typedef struct {
    float level;      // 0..1 AGC-normalized broadband level
    float bands[3];   // 0..1 AGC-normalized bass / mid / treble
    float beat;       // 0..1 beat envelope: kick hits, or the locked grid's
                      // predicted attack-aligned beats
    float grid;       // 0..1 metronome envelope: evenly spaced ticks at the
                      // locked tempo, beat-anchored; stays 0 while unlocked
    float bpm;        // beat-grid tempo, 0 while unlocked
    uint32_t kicks;   // total detected kicks
    float spl_db;     // slow-averaged sound level, dB SPL (datasheet calibrated)
} audio_features_t;

// Initialize I2S PDM RX and the audio task.
esp_err_t audio_init(void);

// Registered by the model's component so audio does not depend on it.
typedef bool (*audio_infer_fn)(const float *window, beat_infer_t *out);
void audio_set_infer_hook(audio_infer_fn fn);

// .valid is false until the context ring has filled.
void audio_get_infer(beat_infer_t *out);

// Context assembly + inference, last block.
uint32_t audio_infer_cycles(void);

// Grid tracking state.
void audio_track_state(bool *locked, float *strength, float *bpm, float *err);

// Music head: last per-block value, fraction of the window above threshold,
// and the windowed decision. Reported only; it gates nothing.
void audio_music_state(float *prob, float *fraction, bool *open);

// Copy the latest features snapshot (lock-free single-writer).
void audio_get_features(audio_features_t *out);

#ifdef __cplusplus
}
#endif
