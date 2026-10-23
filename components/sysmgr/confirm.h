// confirm.h

#pragma once

#include <stdint.h>
#include <stdbool.h>

void confirm_init(void);
bool confirm_pending(void);

// returns true if the press was consumed
bool confirm_poll(int64_t now_us, bool pressed);
