package com.smartstreetlight.model;

import java.time.Duration;
import java.time.Instant;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

public class DeviceState {

    private static final Duration ONLINE_WINDOW = Duration.ofSeconds(30);

    private final String deviceId;
    private Map<String, Object> latestTelemetry = Map.of();
    private Map<String, Object> latestVision = Map.of();
    private Map<String, Object> latestFault = Map.of();
    private Map<String, Object> latestCommandAck = Map.of();
    private Map<String, Object> latestDeviceInfo = Map.of();
    private Instant telemetryAt;
    private Instant visionAt;
    private Instant faultAt;
    private Instant commandAckAt;
    private Instant deviceInfoAt;
    private Instant lastSeenAt;

    public DeviceState(String deviceId) {
        this.deviceId = deviceId;
    }

    public synchronized void updateTelemetry(Map<String, Object> payload, Instant receivedAt) {
        latestTelemetry = immutableCopy(payload);
        telemetryAt = receivedAt;
        lastSeenAt = receivedAt;
    }

    public synchronized void updateVision(Map<String, Object> payload, Instant receivedAt) {
        latestVision = immutableCopy(payload);
        visionAt = receivedAt;
        lastSeenAt = receivedAt;
    }

    public synchronized void updateFault(Map<String, Object> payload, Instant receivedAt) {
        latestFault = immutableCopy(payload);
        faultAt = receivedAt;
        lastSeenAt = receivedAt;
    }

    public synchronized void updateCommandAck(Map<String, Object> payload, Instant receivedAt) {
        latestCommandAck = immutableCopy(payload);
        commandAckAt = receivedAt;
        lastSeenAt = receivedAt;
    }

    public synchronized void updateDeviceInfo(Map<String, Object> payload, Instant receivedAt) {
        latestDeviceInfo = immutableCopy(payload);
        deviceInfoAt = receivedAt;
        lastSeenAt = receivedAt;
    }

    public synchronized DeviceStateSnapshot snapshot(Instant now) {
        boolean online = lastSeenAt != null && !lastSeenAt.isBefore(now.minus(ONLINE_WINDOW));
        return new DeviceStateSnapshot(
                deviceId,
                online,
                lastSeenAt,
                latestTelemetry,
                telemetryAt,
                latestVision,
                visionAt,
                latestFault,
                faultAt,
                latestCommandAck,
                commandAckAt,
                latestDeviceInfo,
                deviceInfoAt
        );
    }

    private static Map<String, Object> immutableCopy(Map<String, Object> payload) {
        return Collections.unmodifiableMap(new LinkedHashMap<>(payload));
    }
}
