package com.smartstreetlight.dto;

import java.util.List;

public record BatchCommandPublishResponse(
        List<CommandPublishResponse> results
) {
}
