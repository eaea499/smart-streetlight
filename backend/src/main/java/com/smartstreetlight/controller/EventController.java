package com.smartstreetlight.controller;

import com.smartstreetlight.service.DeviceEventService;
import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

@RestController
@RequestMapping("/api/events")
public class EventController {

    private final DeviceEventService deviceEventService;

    public EventController(DeviceEventService deviceEventService) {
        this.deviceEventService = deviceEventService;
    }

    @GetMapping(value = "/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    public SseEmitter stream() {
        return deviceEventService.createStream();
    }
}
