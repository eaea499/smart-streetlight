#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_types.h"

typedef struct {
    bool occupied;
    int people_count;
    float max_confidence;
} vision_update_t;

bool vision_parser_parse(const char *payload,
                         int payload_len,
                         vision_update_t *update,
                         char *error,
                         size_t error_len);
