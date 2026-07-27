#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "camera_service.h"
#include "command_parser.h"
#include "d23_sensor.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fault_detector.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hardware_config.h"
#include "ina219_sensor.h"
#include "mqtt_service.h"
#include "nvs_flash.h"
#include "policy_config.h"
#include "streetlight_control.h"
#include "vision_parser.h"
#include "wifi_manager.h"

static const char *TAG = "smart_streetlight";

static streetlight_state_t s_state = {
    .mode = CONTROL_MODE_AUTO,
};
static QueueHandle_t s_command_queue;
static QueueHandle_t s_vision_queue;
static bool s_publish_requested;
static bool s_command_ack_requested;
static bool s_command_ack_accepted;
static const char *s_command_ack_message;

static void apply_command(const streetlight_command_t *command, int64_t now_ms)
{
    s_state.mode = command->mode;
    if (command->mode == CONTROL_MODE_MANUAL) {
        s_state.manual_streetlight_on = command->manual_streetlight_on;
        s_state.manual_brightness_percent = command->manual_brightness_percent;
        s_state.manual_mode_started_ms = now_ms;
        s_state.manual_timeout_ms = command->manual_timeout_ms >= 0 ?
                                    command->manual_timeout_ms :
                                    MANUAL_OVERRIDE_TIMEOUT_MS;
    } else {
        s_state.manual_streetlight_on = false;
        s_state.manual_brightness_percent = 0;
        s_state.manual_mode_started_ms = 0;
        s_state.manual_timeout_ms = 0;
    }

    s_publish_requested = true;
    ESP_LOGI(TAG, "command applied: mode=%s manualLight=%s manualBrightness=%d%% manualTimeoutMs=%d",
             control_mode_to_string(s_state.mode),
             s_state.manual_streetlight_on ? "ON" : "OFF",
             s_state.manual_brightness_percent,
             s_state.manual_timeout_ms);
    s_command_ack_requested = true;
    s_command_ack_accepted = true;
    s_command_ack_message = "command_applied";
}

static void expire_manual_if_stale(int64_t now_ms)
{
    if (s_state.mode != CONTROL_MODE_MANUAL ||
        s_state.manual_timeout_ms <= 0 ||
        s_state.manual_mode_started_ms <= 0) {
        return;
    }

    int64_t age_ms = now_ms - s_state.manual_mode_started_ms;
    if (age_ms <= s_state.manual_timeout_ms) {
        return;
    }

    ESP_LOGW(TAG, "manual override timeout after %" PRIi64 " ms, return to auto",
             age_ms);
    s_state.mode = CONTROL_MODE_AUTO;
    s_state.manual_streetlight_on = false;
    s_state.manual_brightness_percent = 0;
    s_state.manual_mode_started_ms = 0;
    s_state.manual_timeout_ms = 0;
    s_publish_requested = true;
}

static void apply_vision_update(const vision_update_t *update, int64_t now_ms)
{
    s_state.vision.valid = true;
    s_state.vision.occupied = update->occupied;
    s_state.vision.people_count = update->people_count;
    s_state.vision.max_confidence = update->max_confidence;
    s_state.vision.last_update_ms = now_ms;

    ESP_LOGI(TAG, "vision applied: occupied=%s people=%d confidence=%.3f",
             s_state.vision.occupied ? "true" : "false",
             s_state.vision.people_count,
             s_state.vision.max_confidence);
}

static void expire_vision_if_stale(int64_t now_ms)
{
    if (!s_state.vision.valid) {
        return;
    }

    int64_t age_ms = now_ms - s_state.vision.last_update_ms;
    if (age_ms <= VISION_TIMEOUT_MS) {
        return;
    }

    ESP_LOGW(TAG, "vision timeout: no update for %" PRIi64 " ms, fallback to safe brightness",
             age_ms);
    s_state.vision.valid = false;
    s_state.vision.occupied = false;
    s_state.vision.people_count = 0;
    s_state.vision.max_confidence = 0.0f;
    s_publish_requested = true;
}

static void handle_mqtt_command(const char *payload, int payload_len)
{
    streetlight_command_t command = {0};
    char error[64];

    if (!command_parser_parse(payload, payload_len, &command, error, sizeof(error))) {
        ESP_LOGW(TAG, "invalid command: %s", error);
        mqtt_service_publish_command_ack(false, error, &s_state);
        return;
    }

    if (s_command_queue == NULL || xQueueSendToBack(s_command_queue, &command, 0) != pdTRUE) {
        ESP_LOGW(TAG, "command queue full, dropping command");
        mqtt_service_publish_command_ack(false, "command_queue_full", &s_state);
    }
}

static void handle_mqtt_vision(const char *payload, int payload_len)
{
    vision_update_t update = {0};
    char error[64];

    if (!vision_parser_parse(payload, payload_len, &update, error, sizeof(error))) {
        ESP_LOGW(TAG, "invalid vision payload: %s", error);
        return;
    }

    if (s_vision_queue == NULL || xQueueSendToBack(s_vision_queue, &update, 0) != pdTRUE) {
        ESP_LOGW(TAG, "vision queue full, dropping vision payload");
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    d23_sensor_init();
    streetlight_control_init();
    ina219_sensor_init();

    ESP_LOGI(TAG, "Smart streetlight node started");
    ESP_LOGI(TAG, "D23 SIG GPIO=%d, streetlight GPIO=%d", D23_SIG_GPIO, STREETLIGHT_GPIO);

    wifi_manager_connect();
    esp_err_t camera_ret = camera_service_start();
    if (camera_ret != ESP_OK) {
        ESP_LOGW(TAG, "camera service unavailable: %s", esp_err_to_name(camera_ret));
    }

    s_command_queue = xQueueCreate(4, sizeof(streetlight_command_t));
    ESP_ERROR_CHECK(s_command_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    s_vision_queue = xQueueCreate(4, sizeof(vision_update_t));
    ESP_ERROR_CHECK(s_vision_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    mqtt_service_start(handle_mqtt_command, handle_mqtt_vision);

    int64_t last_publish_ms = -TELEMETRY_INTERVAL_MS;

    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        bool previous_light_on = s_state.streetlight_on;
        int previous_brightness_percent = s_state.brightness_percent;
        fault_state_t previous_fault = s_state.fault;
        streetlight_command_t command = {0};
        vision_update_t vision_update = {0};

        while (xQueueReceive(s_command_queue, &command, 0) == pdTRUE) {
            apply_command(&command, now_ms);
        }
        expire_manual_if_stale(now_ms);

        while (xQueueReceive(s_vision_queue, &vision_update, 0) == pdTRUE) {
            apply_vision_update(&vision_update, now_ms);
        }
        expire_vision_if_stale(now_ms);

        d23_sensor_update(&s_state);
        streetlight_control_update(&s_state, now_ms);
        ina219_sensor_update(&s_state);
        fault_detector_update(&s_state, now_ms);
        if (s_state.fault != previous_fault) {
            if (s_state.fault == FAULT_NONE) {
                ESP_LOGI(TAG, "fault cleared");
            } else {
                ESP_LOGW(TAG, "fault changed: %s", fault_state_to_string(s_state.fault));
            }
            mqtt_service_publish_fault_event(&s_state);
            s_publish_requested = true;
        }

        if (s_command_ack_requested) {
            mqtt_service_publish_command_ack(s_command_ack_accepted, s_command_ack_message, &s_state);
            s_command_ack_requested = false;
        }

        bool should_publish = (now_ms - last_publish_ms >= TELEMETRY_INTERVAL_MS) ||
                              (s_state.streetlight_on != previous_light_on) ||
                              (s_state.brightness_percent != previous_brightness_percent) ||
                              s_publish_requested;

        ESP_LOGI(TAG,
                 "mode=%s D23=%d raw_dark=%d dark_count=%d bright_count=%d streetlight=%s "
                 "brightness=%d%% ina219=%s bus=%.3fV shunt=%.3fmV current=%.3fmA "
                 "vision=%s occupied=%s people=%d confidence=%.3f fault=%s",
                 control_mode_to_string(s_state.mode),
                 s_state.d23_level,
                 s_state.raw_is_dark,
                 s_state.dark_count,
                 s_state.bright_count,
                 s_state.streetlight_on ? "ON" : "OFF",
                 s_state.brightness_percent,
                 s_state.ina219_ok ? "OK" : "ERR",
                 s_state.bus_voltage_v,
                 s_state.shunt_voltage_mv,
                 s_state.current_ma,
                 s_state.vision.valid ? "valid" : "none",
                 s_state.vision.occupied ? "true" : "false",
                 s_state.vision.people_count,
                 s_state.vision.max_confidence,
                 fault_state_to_string(s_state.fault));

        if (should_publish) {
            mqtt_service_publish_telemetry(&s_state);
            last_publish_ms = now_ms;
            s_publish_requested = false;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
