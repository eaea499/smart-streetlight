package com.smartstreetlight.service;

import java.io.IOException;
import java.time.Instant;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

import com.smartstreetlight.model.DeviceEvent;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

@Service
public class DeviceEventService {

    private static final Logger log = LoggerFactory.getLogger(DeviceEventService.class);
    private static final long NO_TIMEOUT = 0L;

    private final Set<SseEmitter> emitters = ConcurrentHashMap.newKeySet();

    public SseEmitter createStream() {
        SseEmitter emitter = new SseEmitter(NO_TIMEOUT);
        emitters.add(emitter);

        emitter.onCompletion(() -> emitters.remove(emitter));
        emitter.onTimeout(() -> emitters.remove(emitter));
        emitter.onError(error -> emitters.remove(emitter));

        try {
            emitter.send(SseEmitter.event()
                    .name("connected")
                    .data(new StreamConnectedEvent(Instant.now())));
        } catch (IOException ex) {
            emitters.remove(emitter);
        }

        return emitter;
    }

    public void publish(DeviceEvent event) {
        if (emitters.isEmpty()) {
            return;
        }

        emitters.removeIf(emitter -> !send(emitter, event));
    }

    private boolean send(SseEmitter emitter, DeviceEvent event) {
        try {
            emitter.send(SseEmitter.event()
                    .name("device-state")
                    .id(event.receivedAt().toEpochMilli() + "-" + event.deviceId() + "-" + event.messageType())
                    .data(event));
            return true;
        } catch (IOException | IllegalStateException ex) {
            log.debug("Remove closed SSE emitter", ex);
            return false;
        }
    }

    public record StreamConnectedEvent(Instant connectedAt) {
    }
}
