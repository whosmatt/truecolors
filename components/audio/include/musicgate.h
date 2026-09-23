// musicgate.h
// Windowed decision from the model's per-block music probability. Reported
// only; it does not gate anything yet.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUSICGATE_WIN      188    // ~2 s; the head was measured per take
#define MUSICGATE_THRESH   0.7f
#define MUSICGATE_HOLDOFF  281    // ~3 s

typedef struct {
    uint8_t ring[MUSICGATE_WIN];
    int head;
    int n;
    int above;
    int holdoff;
    bool open;
    float last;
} musicgate_t;

void musicgate_init(musicgate_t *g);
bool musicgate_block(musicgate_t *g, float music_prob);
float musicgate_fraction(const musicgate_t *g);

#ifdef __cplusplus
}
#endif
