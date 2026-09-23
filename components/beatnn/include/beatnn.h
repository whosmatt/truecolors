// beatnn.h
// Learned beat detector: 2.87 s of feature context in; beat activation,
// sub-block offset, hit class and music probability out.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "beat_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "beatnn_classes.h"

// Refuses a model whose front end does not match the compiled one.
esp_err_t beatnn_init(void);
bool beatnn_ready(void);

// One inference on an un-normalised FE_CTX_INPUTS window. Registered with the
// audio task by beatnn_init().
bool beatnn_infer(const float *window, beat_infer_t *out);

uint32_t beatnn_arena_used(void);
uint32_t beatnn_last_cycles(void);
uint32_t beatnn_quant_cycles(void);
uint32_t beatnn_invoke_cycles(void);
void beatnn_selftest_result(beat_infer_t *out);
bool beatnn_selftest_passed(void);  // golden windows reproduced
uint8_t beatnn_input_align(void);   // must be 0, or esp-nn goes scalar

#ifdef __cplusplus
}
#endif
