#include "command_parser.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool parse_string_value(const char *value_start, char *out, size_t out_len)
{
    if (value_start == NULL || out == NULL || out_len == 0 || *value_start != '"') {
        return false;
    }

    value_start++;
    const char *end = strchr(value_start, '"');
    if (end == NULL) {
        return false;
    }

    size_t len = (size_t)(end - value_start);
    if (len >= out_len) {
        return false;
    }

    memcpy(out, value_start, len);
    out[len] = '\0';
    return true;
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
    if (end == value_start || value < 0 || value > INT_MAX) {
        return false;
    }

    end = (char *)skip_spaces(end);
    if (*end != '\0' && *end != ',' && *end != '}' && *end != ']') {
        return false;
    }

    *out = (int)value;
    return true;
}

static bool parse_brightness_percent(const char *value_start, int *out)
{
    int brightness_percent = -1;
    if (!parse_int_value(value_start, &brightness_percent)) {
        return false;
    }
    if (brightness_percent < 0 || brightness_percent > 100) {
        return false;
    }

    *out = brightness_percent;
    return true;
}

bool command_parser_parse(const char *payload,
                          int payload_len,
                          streetlight_command_t *command,
                          char *error,
                          size_t error_len)
{
    if (payload == NULL || payload_len <= 0 || command == NULL) {
        set_error(error, error_len, "empty command");
        return false;
    }
    if (payload_len >= 160) {
        set_error(error, error_len, "command too long");
        return false;
    }

    char json[160];
    memcpy(json, payload, (size_t)payload_len);
    json[payload_len] = '\0';

    char mode[16];
    if (!parse_string_value(find_value_start(json, "mode"), mode, sizeof(mode))) {
        set_error(error, error_len, "missing mode");
        return false;
    }

    if (strcmp(mode, "auto") == 0) {
        command->mode = CONTROL_MODE_AUTO;
        command->manual_streetlight_on = false;
        command->manual_brightness_percent = 0;
        command->manual_timeout_ms = 0;
        return true;
    }

    if (strcmp(mode, "manual") == 0) {
        bool manual_on = false;
        const char *manual_on_start = find_value_start(json, "streetlightOn");
        bool has_manual_on = manual_on_start != NULL;
        if (has_manual_on && !parse_bool_value(manual_on_start, &manual_on)) {
            set_error(error, error_len, "invalid streetlightOn");
            return false;
        }

        int manual_brightness_percent = -1;
        const char *brightness_start = find_value_start(json, "brightnessPercent");
        if (brightness_start != NULL &&
            !parse_brightness_percent(brightness_start, &manual_brightness_percent)) {
            set_error(error, error_len, "invalid brightnessPercent");
            return false;
        }

        if (brightness_start == NULL && !has_manual_on) {
            set_error(error, error_len, "manual mode requires streetlightOn or brightnessPercent");
            return false;
        }

        if (brightness_start != NULL) {
            bool brightness_means_on = manual_brightness_percent > 0;
            if (has_manual_on && manual_on != brightness_means_on) {
                set_error(error, error_len, "streetlightOn conflicts with brightnessPercent");
                return false;
            }
            manual_on = brightness_means_on;
        } else {
            manual_brightness_percent = manual_on ? 100 : 0;
        }

        int manual_timeout_ms = -1;
        const char *timeout_start = find_value_start(json, "manualTimeoutMs");
        if (timeout_start != NULL && !parse_int_value(timeout_start, &manual_timeout_ms)) {
            set_error(error, error_len, "invalid manualTimeoutMs");
            return false;
        }
        command->mode = CONTROL_MODE_MANUAL;
        command->manual_streetlight_on = manual_on;
        command->manual_brightness_percent = manual_brightness_percent;
        command->manual_timeout_ms = manual_timeout_ms;
        return true;
    }

    set_error(error, error_len, "unsupported mode");
    return false;
}
