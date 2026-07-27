#pragma once

#include <stdbool.h>

#include "app_types.h"

typedef void (*mqtt_command_handler_t)(const char *payload, int payload_len);
typedef void (*mqtt_vision_handler_t)(const char *payload, int payload_len);

void mqtt_service_start(mqtt_command_handler_t command_handler,
                        mqtt_vision_handler_t vision_handler);
bool mqtt_service_is_connected(void);
void mqtt_service_publish_telemetry(const streetlight_state_t *state);
void mqtt_service_publish_command_ack(bool accepted, const char *message, const streetlight_state_t *state);
void mqtt_service_publish_fault_event(const streetlight_state_t *state);
