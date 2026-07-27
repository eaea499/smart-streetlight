#pragma once

#include <stdint.h>

#include "app_types.h"

void fault_detector_update(streetlight_state_t *state, int64_t now_ms);
