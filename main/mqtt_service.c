#include "mqtt_service.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "hardware_config.h"
#include "wifi_manager.h"

#ifndef CONFIG_SMART_STREETLIGHT_LOCATION
#define CONFIG_SMART_STREETLIGHT_LOCATION "NCHU Test Point A"
#endif

extern const uint8_t emqxsl_ca_crt_start[] asm("_binary_emqxsl_ca_crt_start");
extern const uint8_t emqxsl_ca_crt_end[] asm("_binary_emqxsl_ca_crt_end");

static const char *TAG = "mqtt_service";

static esp_mqtt_client_handle_t s_mqtt_client;
static bool s_mqtt_connected;
static mqtt_command_handler_t s_command_handler;
static mqtt_vision_handler_t s_vision_handler;
static char s_telemetry_topic[96];
static char s_command_topic[96];
static char s_broadcast_command_topic[96];
static char s_command_ack_topic[96];
static char s_vision_topic[96];
static char s_fault_topic[96];
static char s_device_info_topic[96];

static const char *fault_message(fault_state_t fault)
{
    switch (fault) {
    case FAULT_NONE:
        return "fault cleared";
    case FAULT_INA219_OFFLINE:
        return "INA219 current sensor is offline";
    case FAULT_OPEN_LOAD:
        return "streetlight may be disconnected or LED failed";
    case FAULT_UNEXPECTED_CURRENT:
        return "unexpected current detected while streetlight is off";
    case FAULT_OVER_CURRENT:
        return "streetlight current is too high";
    default:
        return "unknown fault";
    }
}

static bool event_topic_matches(const esp_mqtt_event_handle_t event, const char *topic)
{
    size_t topic_len = strlen(topic);
    return event->topic != NULL &&
           (size_t)event->topic_len == topic_len &&
           strncmp(event->topic, topic, topic_len) == 0;
}

static void publish_device_info(void)
{
    const char *ip_address = wifi_manager_get_ip_address();
    if (ip_address == NULL || ip_address[0] == '\0') {
        ESP_LOGW(TAG, "skip device info, IP address is empty");
        return;
    }

    char camera_base_url[40];
    if (CAMERA_HTTP_PORT == 80) {
        snprintf(camera_base_url, sizeof(camera_base_url), "http://%s", ip_address);
    } else {
        snprintf(camera_base_url, sizeof(camera_base_url), "http://%s:%d", ip_address, CAMERA_HTTP_PORT);
    }

    char payload[192];
    int64_t uptime_ms = esp_timer_get_time() / 1000;
    snprintf(payload, sizeof(payload),
             "{\"deviceId\":\"%s\",\"uptimeMs\":%" PRIi64 ",\"cameraBaseUrl\":\"%s\"}",
             CONFIG_SMART_STREETLIGHT_DEVICE_ID,
             uptime_ms,
             camera_base_url);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_device_info_topic, payload, 0, 1, 1);
    ESP_LOGI(TAG, "device info msg_id=%d topic=%s payload=%s", msg_id, s_device_info_topic, payload);
}

static void handle_command_event(const esp_mqtt_event_handle_t event, const char *matched_topic)
{
    if (event->current_data_offset == 0 && event->data_len == event->total_data_len) {
        ESP_LOGI(TAG, "command topic=%s payload=%.*s", matched_topic, event->data_len, event->data);
        if (s_command_handler != NULL) {
            s_command_handler(event->data, event->data_len);
        }
    } else {
        ESP_LOGW(TAG, "ignore fragmented command payload");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_mqtt_connected = true;
        int command_msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_command_topic, 1);
        int broadcast_command_msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_broadcast_command_topic, 1);
        int vision_msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_vision_topic, 0);
        publish_device_info();
        ESP_LOGI(TAG,
                 "connected, subscribe command=%s msg_id=%d broadcast=%s msg_id=%d vision=%s msg_id=%d",
                 s_command_topic, command_msg_id,
                 s_broadcast_command_topic, broadcast_command_msg_id,
                 s_vision_topic, vision_msg_id);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        ESP_LOGW(TAG, "disconnected");
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "published, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "subscribed, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        if (event_topic_matches(event, s_command_topic)) {
            handle_command_event(event, s_command_topic);
        } else if (event_topic_matches(event, s_broadcast_command_topic)) {
            handle_command_event(event, s_broadcast_command_topic);
        } else if (event_topic_matches(event, s_vision_topic)) {
            if (event->current_data_offset == 0 && event->data_len == event->total_data_len) {
                ESP_LOGI(TAG, "vision topic=%s payload=%.*s", s_vision_topic, event->data_len, event->data);
                if (s_vision_handler != NULL) {
                    s_vision_handler(event->data, event->data_len);
                }
            } else {
                ESP_LOGW(TAG, "ignore fragmented vision payload");
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "event error");
        if (event->error_handle) {
            ESP_LOGE(TAG, "esp_tls_last_esp_err=0x%x, tls_stack_err=0x%x, sock_errno=%d",
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_tls_stack_err,
                     event->error_handle->esp_transport_sock_errno);
        }
        break;
    default:
        break;
    }
}

void mqtt_service_start(mqtt_command_handler_t command_handler,
                        mqtt_vision_handler_t vision_handler)
{
    s_command_handler = command_handler;
    s_vision_handler = vision_handler;
    snprintf(s_telemetry_topic, sizeof(s_telemetry_topic),
             "streetlight/%s/telemetry", CONFIG_SMART_STREETLIGHT_DEVICE_ID);
    snprintf(s_command_topic, sizeof(s_command_topic),
             "streetlight/%s/command", CONFIG_SMART_STREETLIGHT_DEVICE_ID);
    snprintf(s_broadcast_command_topic, sizeof(s_broadcast_command_topic),
             "streetlight/all/command");
    snprintf(s_command_ack_topic, sizeof(s_command_ack_topic),
             "streetlight/%s/commandAck", CONFIG_SMART_STREETLIGHT_DEVICE_ID);
    snprintf(s_vision_topic, sizeof(s_vision_topic),
             "streetlight/%s/vision", CONFIG_SMART_STREETLIGHT_DEVICE_ID);
    snprintf(s_fault_topic, sizeof(s_fault_topic),
             "streetlight/%s/fault", CONFIG_SMART_STREETLIGHT_DEVICE_ID);
    snprintf(s_device_info_topic, sizeof(s_device_info_topic),
             "streetlight/%s/deviceInfo", CONFIG_SMART_STREETLIGHT_DEVICE_ID);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = CONFIG_SMART_STREETLIGHT_MQTT_URI,
            .verification.certificate = (const char *)emqxsl_ca_crt_start,
        },
        .credentials = {
            .client_id = CONFIG_SMART_STREETLIGHT_DEVICE_ID,
            .username = CONFIG_SMART_STREETLIGHT_MQTT_USERNAME,
            .authentication.password = CONFIG_SMART_STREETLIGHT_MQTT_PASSWORD,
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));
}

bool mqtt_service_is_connected(void)
{
    return s_mqtt_connected && s_mqtt_client != NULL;
}

void mqtt_service_publish_telemetry(const streetlight_state_t *state)
{
    if (!mqtt_service_is_connected()) {
        ESP_LOGW(TAG, "not connected, skip telemetry");
        return;
    }

    char payload[576];
    int64_t uptime_ms = esp_timer_get_time() / 1000;

    snprintf(payload, sizeof(payload),
             "{\"deviceId\":\"%s\",\"uptimeMs\":%" PRIi64 ",\"d23Level\":%d,"
             "\"dark\":%s,\"streetlightOn\":%s,\"brightnessPercent\":%d,\"mode\":\"%s\","
             "\"manualTimeoutMs\":%d,"
             "\"ina219Ok\":%s,\"busVoltageV\":%.3f,\"shuntVoltageMv\":%.3f,"
             "\"currentMa\":%.3f,\"visionValid\":%s,\"occupied\":%s,"
             "\"peopleCount\":%d,\"visionConfidence\":%.3f,\"fault\":\"%s\"}",
             CONFIG_SMART_STREETLIGHT_DEVICE_ID,
             uptime_ms,
             state->d23_level,
             state->raw_is_dark ? "true" : "false",
             state->streetlight_on ? "true" : "false",
             state->brightness_percent,
             control_mode_to_string(state->mode),
             state->manual_timeout_ms,
             state->ina219_ok ? "true" : "false",
             state->bus_voltage_v,
             state->shunt_voltage_mv,
             state->current_ma,
             state->vision.valid ? "true" : "false",
             state->vision.occupied ? "true" : "false",
             state->vision.people_count,
             state->vision.max_confidence,
             fault_state_to_string(state->fault));

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_telemetry_topic, payload, 0, 1, 0);
    ESP_LOGI(TAG, "telemetry msg_id=%d topic=%s payload=%s", msg_id, s_telemetry_topic, payload);
}

void mqtt_service_publish_command_ack(bool accepted, const char *message, const streetlight_state_t *state)
{
    if (!mqtt_service_is_connected()) {
        ESP_LOGW(TAG, "not connected, skip command ack");
        return;
    }

    char payload[320];
    int64_t uptime_ms = esp_timer_get_time() / 1000;

    snprintf(payload, sizeof(payload),
             "{\"deviceId\":\"%s\",\"uptimeMs\":%" PRIi64 ",\"accepted\":%s,"
             "\"message\":\"%s\",\"mode\":\"%s\",\"streetlightOn\":%s,"
             "\"brightnessPercent\":%d,\"manualTimeoutMs\":%d}",
             CONFIG_SMART_STREETLIGHT_DEVICE_ID,
             uptime_ms,
             accepted ? "true" : "false",
             message != NULL ? message : "",
             control_mode_to_string(state->mode),
             state->streetlight_on ? "true" : "false",
             state->brightness_percent,
             state->manual_timeout_ms);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_command_ack_topic, payload, 0, 1, 0);
    ESP_LOGI(TAG, "command ack msg_id=%d topic=%s payload=%s", msg_id, s_command_ack_topic, payload);
}

void mqtt_service_publish_fault_event(const streetlight_state_t *state)
{
    if (!mqtt_service_is_connected()) {
        ESP_LOGW(TAG, "not connected, skip fault event");
        return;
    }

    char payload[640];
    int64_t uptime_ms = esp_timer_get_time() / 1000;

    snprintf(payload, sizeof(payload),
             "{\"deviceId\":\"%s\",\"uptimeMs\":%" PRIi64 ",\"fault\":\"%s\","
             "\"repairRequired\":%s,\"message\":\"%s\",\"location\":\"%s\","
             "\"mode\":\"%s\",\"streetlightOn\":%s,\"brightnessPercent\":%d,"
             "\"ina219Ok\":%s,\"busVoltageV\":%.3f,\"shuntVoltageMv\":%.3f,"
             "\"currentMa\":%.3f}",
             CONFIG_SMART_STREETLIGHT_DEVICE_ID,
             uptime_ms,
             fault_state_to_string(state->fault),
             state->fault == FAULT_NONE ? "false" : "true",
             fault_message(state->fault),
             CONFIG_SMART_STREETLIGHT_LOCATION,
             control_mode_to_string(state->mode),
             state->streetlight_on ? "true" : "false",
             state->brightness_percent,
             state->ina219_ok ? "true" : "false",
             state->bus_voltage_v,
             state->shunt_voltage_mv,
             state->current_ma);

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_fault_topic, payload, 0, 1, 0);
    ESP_LOGI(TAG, "fault event msg_id=%d topic=%s payload=%s", msg_id, s_fault_topic, payload);
}
