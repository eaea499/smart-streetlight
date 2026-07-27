#include "streetlight_control.h"

#include <stdint.h>

#include "driver/ledc.h"
#include "esp_err.h"
#include "hardware_config.h"
#include "streetlight_policy.h"

#define STREETLIGHT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define STREETLIGHT_LEDC_TIMER LEDC_TIMER_0
#define STREETLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define STREETLIGHT_LEDC_DUTY_RES LEDC_TIMER_10_BIT

static uint32_t brightness_percent_to_duty(int brightness_percent)
{
    if (brightness_percent <= 0) {
        return 0;
    }
    if (brightness_percent >= 100) {
        return STREETLIGHT_PWM_MAX_DUTY;
    }

    return (uint32_t)((brightness_percent * STREETLIGHT_PWM_MAX_DUTY + 50) / 100);
}

void streetlight_control_init(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = STREETLIGHT_LEDC_MODE,
        .duty_resolution = STREETLIGHT_LEDC_DUTY_RES,
        .timer_num = STREETLIGHT_LEDC_TIMER,
        .freq_hz = STREETLIGHT_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num = STREETLIGHT_GPIO,
        .speed_mode = STREETLIGHT_LEDC_MODE,
        .channel = STREETLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = STREETLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

void streetlight_control_update(streetlight_state_t *state, int64_t now_ms)
{
    bool previous_light_on = state->streetlight_on;
    int previous_brightness_percent = state->brightness_percent;

    state->brightness_percent = streetlight_policy_calculate_brightness_percent(state);
    state->streetlight_on = state->brightness_percent > 0;

    if (state->streetlight_on != previous_light_on ||
        state->brightness_percent != previous_brightness_percent) {
        state->light_state_changed_ms = now_ms;
    }

    uint32_t duty = brightness_percent_to_duty(state->brightness_percent);
    ESP_ERROR_CHECK(ledc_set_duty(STREETLIGHT_LEDC_MODE, STREETLIGHT_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(STREETLIGHT_LEDC_MODE, STREETLIGHT_LEDC_CHANNEL));
}
