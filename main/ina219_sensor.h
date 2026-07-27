#pragma once

#include "app_types.h"

void ina219_sensor_init(void);
void ina219_sensor_update(streetlight_state_t *state);
