// tempo.h
// Period and phase from the model's beat activation. Port of the estimator the
// model was measured against; see temp_ML2/specs/stage2-spec.md.
// Batch fit over a whole window, no state carried between calls.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEMPO_BLOCK_HZ    93.75f
#define TEMPO_WIN_BLOCKS  750     // 8 s
#define TEMPO_BPM_MIN     55.0f
#define TEMPO_BPM_MAX     220.0f

// lag = 60 * 93.75 / bpm, so 25.57 .. 102.27 blocks; harmonic summation reaches
// 3x the longest. Literals because a float cast is not a constant expression
// at file scope; test_tempo.c asserts they still bracket the range.
#define TEMPO_LAG_MAX 104
#define TEMPO_AC_MAX  314

typedef struct {
    bool valid;
    float bpm;
    float period;    // blocks
    float phase;     // blocks from the oldest sample to a beat
    float strength;  // mean activation on the fitted grid
} tempo_est_t;

typedef struct {
    float ring[TEMPO_WIN_BLOCKS];
    int head;
    int n;
} tempo_buf_t;

void tempo_init(tempo_buf_t *b);
void tempo_push(tempo_buf_t *b, float activation);

// ~500k float ops: call every N blocks, not every block. False until full.
bool tempo_estimate(const tempo_buf_t *b, tempo_est_t *out);

// Blocks from the newest sample to the next beat.
float tempo_next_beat_in(const tempo_est_t *e);

#ifdef __cplusplus
}
#endif
