#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CONTROL_MODE_AUTO = 0,
    CONTROL_MODE_MANUAL,
} control_mode_t;

typedef enum {
    FAULT_NONE = 0,
    FAULT_INA219_OFFLINE,
    FAULT_OPEN_LOAD,
    FAULT_UNEXPECTED_CURRENT,
    FAULT_OVER_CURRENT,
} fault_state_t;

typedef struct {
    bool valid;
    bool occupied;
    int people_count;
    float max_confidence;
    int64_t last_update_ms;
} vision_state_t;

typedef struct {
    int d23_level;
    bool raw_is_dark;
    int dark_count;
    int bright_count;

    control_mode_t mode;
    bool manual_streetlight_on;
    int manual_brightness_percent;
    int manual_timeout_ms;
    int64_t manual_mode_started_ms;
    bool streetlight_on;
    int brightness_percent;
    int64_t light_state_changed_ms;

    bool ina219_ok;
    float bus_voltage_v;
    float shunt_voltage_mv;
    float current_ma;

    vision_state_t vision;

    fault_state_t fault;
} streetlight_state_t;

static inline const char *control_mode_to_string(control_mode_t mode)
{
    switch (mode) {
    case CONTROL_MODE_AUTO:
        return "auto";
    case CONTROL_MODE_MANUAL:
        return "manual";
    default:
        return "unknown";
    }
}

static inline const char *fault_state_to_string(fault_state_t fault)
{
    switch (fault) {
    case FAULT_NONE:
        return "none";
    case FAULT_INA219_OFFLINE:
        return "ina219_offline";
    case FAULT_OPEN_LOAD:
        return "open_load";
    case FAULT_UNEXPECTED_CURRENT:
        return "unexpected_current";
    case FAULT_OVER_CURRENT:
        return "over_current";
    default:
        return "unknown";
    }
}
