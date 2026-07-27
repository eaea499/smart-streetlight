package com.smartstreetlight.model;

import java.time.Instant;
import java.util.Map;

public record FaultEvent(
        String deviceId,
        Instant receivedAt,
        String fault,
        Boolean repairRequired,
        String message,
        String location,
        Map<String, Object> payload
) {

    public static FaultEvent from(String deviceId, Instant receivedAt, Map<String, Object> payload) {
        return new FaultEvent(
                deviceId,
                receivedAt,
                asString(payload.get("fault")),
                asBoolean(payload.get("repairRequired")),
                asString(payload.get("message")),
                asString(payload.get("location")),
                payload
        );
    }

    private static String asString(Object value) {
        return value == null ? null : String.valueOf(value);
    }

    private static Boolean asBoolean(Object value) {
        if (value instanceof Boolean bool) {
            return bool;
        }
        if (value == null) {
            return null;
        }
        return Boolean.parseBoolean(String.valueOf(value));
    }
}
