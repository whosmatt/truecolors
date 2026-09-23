#include "musicgate.h"

#include <string.h>

void musicgate_init(musicgate_t *g)
{
    memset(g, 0, sizeof(*g));
}

bool musicgate_block(musicgate_t *g, float music_prob)
{
    g->last = music_prob;
    uint8_t v = music_prob >= MUSICGATE_THRESH ? 1u : 0u;

    if (g->n == MUSICGATE_WIN) {
        g->above -= g->ring[g->head];
    } else {
        g->n++;
    }
    g->ring[g->head] = v;
    g->above += v;
    g->head = (g->head + 1) % MUSICGATE_WIN;

    // median >= threshold is "more than half the window above it", so no sort.
    bool music = g->above * 2 > g->n;

    if (music) {
        g->open = true;
        g->holdoff = MUSICGATE_HOLDOFF;
    } else if (g->holdoff > 0) {
        g->holdoff--;
    } else {
        g->open = false;
    }
    return g->open;
}

float musicgate_fraction(const musicgate_t *g)
{
    return g->n ? (float)g->above / (float)g->n : 0.0f;
}
