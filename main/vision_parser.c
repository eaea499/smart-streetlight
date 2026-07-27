#include "vision_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VISION_PAYLOAD_MAX_LEN 512

static void set_error(char *error, size_t error_len, const char *message)
{
    if (error != NULL && error_len > 0) {
        snprintf(error, error_len, "%s", message);
    }
}

static const char *skip_spaces(const char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    return text;
}

static const char *find_value_start(const char *json, const char *key)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *key_pos = strstr(json, pattern);
    if (key_pos == NULL) {
        return NULL;
    }

    const char *colon = strchr(key_pos + strlen(pattern), ':');
    if (colon == NULL) {
        return NULL;
    }

    return skip_spaces(colon + 1);
}

static bool parse_bool_value(const char *value_start, bool *out)
{
    if (value_start == NULL || out == NULL) {
        return false;
    }

    if (strncmp(value_start, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(value_start, "false", 5) == 0) {
        *out = false;
        return true;
    }

    return false;
}

static bool parse_int_value(const char *value_start, int *out)
{
    if (value_start == NULL || out == NULL) {
        return false;
    }

    char *end = NULL;
    long value = strtol(value_start, &end, 10);
    if (end == value_start) {
        return false;
    }

    *out = (int)value;
    return true;
}

static bool parse_float_value(const char *value_start, float *out)
{
    if (value_start == NULL || out == NULL) {
        return false;
    }

    char *end = NULL;
    float value = strtof(value_start, &end);
    if (end == value_start) {
        return false;
    }

    *out = value;
    return true;
}

bool vision_parser_parse(const char *payload,
                         int payload_len,
                         vision_update_t *update,
                         char *error,
                         size_t error_len)
{
    if (payload == NULL || payload_len <= 0 || update == NULL) {
        set_error(error, error_len, "empty vision payload");
        return false;
    }
    if (payload_len >= VISION_PAYLOAD_MAX_LEN) {
        set_error(error, error_len, "vision payload too long");
        return false;
    }

    char json[VISION_PAYLOAD_MAX_LEN];
    memcpy(json, payload, (size_t)payload_len);
    json[payload_len] = '\0';

    bool occupied = false;
    if (!parse_bool_value(find_value_start(json, "occupied"), &occupied)) {
        set_error(error, error_len, "missing occupied");
        return false;
    }

    int people_count = 0;
    if (!parse_int_value(find_value_start(json, "peopleCount"), &people_count)) {
        set_error(error, error_len, "missing peopleCount");
        return false;
    }

    float max_confidence = 0.0f;
    const char *confidence_start = find_value_start(json, "maxConfidence");
    if (confidence_start != NULL && !parse_float_value(confidence_start, &max_confidence)) {
        set_error(error, error_len, "invalid maxConfidence");
        return false;
    }

    update->occupied = occupied;
    update->people_count = people_count;
    update->max_confidence = max_confidence;
    return true;
}
