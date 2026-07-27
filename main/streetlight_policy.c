#include "streetlight_policy.h"

#include "policy_config.h"

int streetlight_policy_calculate_brightness_percent(const streetlight_state_t *state)
{
    if (state->mode == CONTROL_MODE_MANUAL) {
        if (state->manual_brightness_percent <= 0) {
            return 0;
        }
        if (state->manual_brightness_percent >= 100) {
            return 100;
        }
        return state->manual_brightness_percent;
    }

    if (state->bright_count >= LIGHT_CONFIRM_COUNT) {
        return 0;
    }

    if (state->dark_count >= LIGHT_CONFIRM_COUNT) {
        if (state->vision.valid && !state->vision.occupied) {
            return UNOCCUPIED_BRIGHTNESS_PERCENT;
        }
        return 100;
    }

    return state->brightness_percent;
}
