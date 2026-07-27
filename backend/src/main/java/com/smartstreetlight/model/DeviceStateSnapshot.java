package com.smartstreetlight.model;

import java.time.Instant;
import java.util.Map;

public record DeviceStateSnapshot(
        String deviceId,
        boolean online,
        Instant lastSeenAt,
        Map<String, Object> latestTelemetry,
        Instant telemetryAt,
        Map<String, Object> latestVision,
        Instant visionAt,
        Map<String, Object> latestFault,
        Instant faultAt,
        Map<String, Object> latestCommandAck,
        Instant commandAckAt,
        Map<String, Object> latestDeviceInfo,
        Instant deviceInfoAt
) {
}
