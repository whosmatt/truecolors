// fe_ctx.h
// Feature history for the beat model: 2.87 s of context at three resolutions.
// Pure C99, state in fe_ctx_t, so truecolors-ml compiles it too.
#pragma once

#include <stdbool.h>
#include "frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FE_CTX_FEATS         12
#define FE_CTX_FINE          16   // one block each, t-13 .. t+2
#define FE_CTX_MID           16   // mean of 4, walking back from t-13
#define FE_CTX_MID_STRIDE     4
#define FE_CTX_COARSE        12   // mean of 16, walking back from t-77
#define FE_CTX_COARSE_STRIDE 16

// Output for block t is produced at t+2; the grid subtracts it when placing
// an anchor.
#define FE_CTX_LOOKAHEAD      2

#define FE_CTX_FRAMES  (FE_CTX_FINE + FE_CTX_MID + FE_CTX_COARSE)          // 44
#define FE_CTX_INPUTS  (FE_CTX_FRAMES * FE_CTX_FEATS)                      // 528
#define FE_CTX_BLOCKS  (FE_CTX_FINE + FE_CTX_MID * FE_CTX_MID_STRIDE + \
                        FE_CTX_COARSE * FE_CTX_COARSE_STRIDE)              // 272

typedef struct {
    float ring[FE_CTX_BLOCKS][FE_CTX_FEATS];
    int head;
    int n;
} fe_ctx_t;

void fe_ctx_init(fe_ctx_t *c);
void fe_ctx_push(fe_ctx_t *c, const fe_out_t *f);

// FE_CTX_INPUTS floats, un-normalised. False until the ring fills.
bool fe_ctx_window(const fe_ctx_t *c, float *out);

#ifdef __cplusplus
}
#endif
