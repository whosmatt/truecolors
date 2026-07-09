// snare.h
// Snare/clap/rim classification criteria, applied by kick.c's onset state
// machine to the anchored attack features. Stateless.
#pragma once

#include <stdbool.h>
#include "kick.h"

#ifdef __cplusplus
extern "C" {
#endif

bool snare_classify(const kick_in_t *anchor);

#ifdef __cplusplus
}
#endif
