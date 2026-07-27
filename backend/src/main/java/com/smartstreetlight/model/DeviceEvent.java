package com.smartstreetlight.model;

import java.time.Instant;
import java.util.Map;

public record DeviceEvent(
        String deviceId,
        String messageType,
        Instant receivedAt,
        Map<String, Object> payload,
        DeviceStateSnapshot device
) {
}
