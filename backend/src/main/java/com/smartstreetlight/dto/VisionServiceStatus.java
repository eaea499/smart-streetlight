package com.smartstreetlight.dto;

import java.time.Instant;
import java.util.List;

public record VisionServiceStatus(
        boolean running,
        String state,
        String deviceId,
        String source,
        Instant startedAt,
        Instant stoppedAt,
        Integer exitCode,
        String message,
        List<String> recentLogs
) {
}
