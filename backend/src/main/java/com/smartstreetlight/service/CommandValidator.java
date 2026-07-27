package com.smartstreetlight.service;

import com.smartstreetlight.dto.CommandRequest;
import org.springframework.stereotype.Component;
import org.springframework.web.server.ResponseStatusException;

import static org.springframework.http.HttpStatus.BAD_REQUEST;

@Component
public class CommandValidator {

    public void validate(CommandRequest request) {
        String mode = request.normalizedMode();
        if (!mode.equals("auto") && !mode.equals("manual")) {
            throw new ResponseStatusException(BAD_REQUEST, "mode must be auto or manual");
        }
        if (request.brightnessPercent() != null &&
            (request.brightnessPercent() < 0 || request.brightnessPercent() > 100)) {
            throw new ResponseStatusException(BAD_REQUEST, "brightnessPercent must be between 0 and 100");
        }
        if (mode.equals("manual") &&
            request.streetlightOn() == null &&
            request.brightnessPercent() == null) {
            throw new ResponseStatusException(BAD_REQUEST, "manual mode requires streetlightOn or brightnessPercent");
        }
        if (mode.equals("manual") &&
            request.streetlightOn() != null &&
            request.brightnessPercent() != null &&
            request.streetlightOn() != (request.brightnessPercent() > 0)) {
            throw new ResponseStatusException(BAD_REQUEST, "streetlightOn conflicts with brightnessPercent");
        }
        if (request.manualTimeoutMs() != null && request.manualTimeoutMs() < 0) {
            throw new ResponseStatusException(BAD_REQUEST, "manualTimeoutMs must be greater than or equal to 0");
        }
    }
}
