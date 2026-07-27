package com.smartstreetlight.dto;

import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

import jakarta.validation.constraints.NotBlank;

public record CommandRequest(
        @NotBlank String mode,
        Boolean streetlightOn,
        Integer brightnessPercent,
        Long manualTimeoutMs
) {

    public String normalizedMode() {
        return mode == null ? "" : mode.trim().toLowerCase(Locale.ROOT);
    }

    public Map<String, Object> toPayload() {
        Map<String, Object> payload = new LinkedHashMap<>();
        payload.put("mode", normalizedMode());
        if (streetlightOn != null) {
            payload.put("streetlightOn", streetlightOn);
        }
        if (brightnessPercent != null) {
            payload.put("brightnessPercent", brightnessPercent);
        }
        if (manualTimeoutMs != null) {
            payload.put("manualTimeoutMs", manualTimeoutMs);
        }
        return payload;
    }
}
