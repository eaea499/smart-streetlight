#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_types.h"

typedef struct {
    control_mode_t mode;
    bool manual_streetlight_on;
    int manual_brightness_percent;
    int manual_timeout_ms;
} streetlight_command_t;

bool command_parser_parse(const char *payload,
                          int payload_len,
                          streetlight_command_t *command,
                          char *error,
                          size_t error_len);
