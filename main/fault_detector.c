#include "fault_detector.h"

#include "hardware_config.h"

static float open_load_threshold_ma(const streetlight_state_t *state)
{
    if (state->brightness_percent >= 100) {
        return LED_OPEN_LOAD_THRESHOLD_MA;
    }
    if (state->brightness_percent <= 0) {
        return 0.0f;
    }

    float scaled_threshold = LED_OPEN_LOAD_THRESHOLD_MA *
                             (float)state->brightness_percent / 100.0f;
    return scaled_threshold < LED_MIN_OPEN_LOAD_THRESHOLD_MA ?
           LED_MIN_OPEN_LOAD_THRESHOLD_MA : scaled_threshold;
}

void fault_detector_update(streetlight_state_t *state, int64_t now_ms)
{
    float abs_current_ma = state->current_ma < 0.0f ? -state->current_ma : state->current_ma;
    bool in_light_transition_grace =
        (now_ms - state->light_state_changed_ms) < FAULT_EVALUATION_GRACE_MS;

    if (!state->ina219_ok) {
        state->fault = FAULT_INA219_OFFLINE;
    } else if (abs_current_ma > LED_OVER_CURRENT_THRESHOLD_MA) {
        state->fault = FAULT_OVER_CURRENT;
    } else if (in_light_transition_grace) {
        state->fault = FAULT_NONE;
    } else if (state->streetlight_on && abs_current_ma < open_load_threshold_ma(state)) {
        state->fault = FAULT_OPEN_LOAD;
    } else if (!state->streetlight_on && abs_current_ma > LED_UNEXPECTED_CURRENT_THRESHOLD_MA) {
        state->fault = FAULT_UNEXPECTED_CURRENT;
    } else {
        state->fault = FAULT_NONE;
    }
}
