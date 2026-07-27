#pragma once

#include <stdint.h>

#include "app_types.h"

void streetlight_control_init(void);
void streetlight_control_update(streetlight_state_t *state, int64_t now_ms);
