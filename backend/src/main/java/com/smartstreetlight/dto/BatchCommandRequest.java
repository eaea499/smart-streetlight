package com.smartstreetlight.dto;

import java.util.List;

import jakarta.validation.constraints.NotBlank;

public record BatchCommandRequest(
        List<String> deviceIds,
        Boolean broadcast,
        @NotBlank String mode,
        Boolean streetlightOn,
        Integer brightnessPercent,
        Long manualTimeoutMs
) {

    public CommandRequest toCommandRequest() {
        return new CommandRequest(mode, streetlightOn, brightnessPercent, manualTimeoutMs);
    }
}
