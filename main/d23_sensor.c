#include "d23_sensor.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "hardware_config.h"

void d23_sensor_init(void)
{
    gpio_config_t d23_config = {
        .pin_bit_mask = 1ULL << D23_SIG_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&d23_config));
}

void d23_sensor_update(streetlight_state_t *state)
{
    state->d23_level = gpio_get_level(D23_SIG_GPIO);
    state->raw_is_dark = (state->d23_level == D23_LEVEL_DARK);

    if (state->raw_is_dark) {
        if (state->dark_count < LIGHT_CONFIRM_COUNT) {
            state->dark_count++;
        }
        state->bright_count = 0;
    } else {
        if (state->bright_count < LIGHT_CONFIRM_COUNT) {
            state->bright_count++;
        }
        state->dark_count = 0;
    }
}
