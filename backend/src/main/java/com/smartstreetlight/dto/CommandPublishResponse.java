package com.smartstreetlight.dto;

import java.time.Instant;
import java.util.Map;

public record CommandPublishResponse(
        String topic,
        Map<String, Object> payload,
        Instant publishedAt
) {
}
