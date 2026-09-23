#include "fe_ctx.h"

#include <string.h>

_Static_assert(sizeof(fe_out_t) == FE_CTX_FEATS * sizeof(float),
               "fe_out_t must be FE_CTX_FEATS contiguous floats");

void fe_ctx_init(fe_ctx_t *c)
{
    memset(c, 0, sizeof(*c));
}

void fe_ctx_push(fe_ctx_t *c, const fe_out_t *f)
{
    memcpy(c->ring[c->head], f, sizeof(fe_out_t));
    c->head = (c->head + 1) % FE_CTX_BLOCKS;
    if (c->n < FE_CTX_BLOCKS) {
        c->n++;
    }
}

// age 0 is the newest block pushed, block t+FE_CTX_LOOKAHEAD.
static const float *at(const fe_ctx_t *c, int age)
{
    int i = c->head - 1 - age;
    while (i < 0) {
        i += FE_CTX_BLOCKS;
    }
    return c->ring[i];
}

static void mean_into(const fe_ctx_t *c, int age, int count, float *out)
{
    for (int k = 0; k < FE_CTX_FEATS; k++) {
        out[k] = 0.0f;
    }
    for (int b = 0; b < count; b++) {
        const float *src = at(c, age + b);
        for (int k = 0; k < FE_CTX_FEATS; k++) {
            out[k] += src[k];
        }
    }
    float inv = 1.0f / (float)count;
    for (int k = 0; k < FE_CTX_FEATS; k++) {
        out[k] *= inv;
    }
}

bool fe_ctx_window(const fe_ctx_t *c, float *out)
{
    if (c->n < FE_CTX_BLOCKS) {
        return false;
    }

    // The three sections do not share a direction: fine runs oldest to newest,
    // mid and coarse walk backwards.
    for (int f = 0; f < FE_CTX_FINE; f++) {
        memcpy(out + f * FE_CTX_FEATS, at(c, FE_CTX_FINE - 1 - f),
               sizeof(float) * FE_CTX_FEATS);
    }

    float *p = out + FE_CTX_FINE * FE_CTX_FEATS;
    for (int f = 0; f < FE_CTX_MID; f++) {
        mean_into(c, FE_CTX_FINE + f * FE_CTX_MID_STRIDE, FE_CTX_MID_STRIDE,
                  p + f * FE_CTX_FEATS);
    }

    p = out + (FE_CTX_FINE + FE_CTX_MID) * FE_CTX_FEATS;
    int base = FE_CTX_FINE + FE_CTX_MID * FE_CTX_MID_STRIDE;
    for (int f = 0; f < FE_CTX_COARSE; f++) {
        mean_into(c, base + f * FE_CTX_COARSE_STRIDE, FE_CTX_COARSE_STRIDE,
                  p + f * FE_CTX_FEATS);
    }
    return true;
}
