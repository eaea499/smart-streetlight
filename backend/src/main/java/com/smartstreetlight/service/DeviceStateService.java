package com.smartstreetlight.service;

import java.time.Instant;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.CopyOnWriteArrayList;

import com.smartstreetlight.model.DeviceState;
import com.smartstreetlight.model.DeviceEvent;
import com.smartstreetlight.model.DeviceStateSnapshot;
import com.smartstreetlight.model.FaultEvent;
import org.springframework.stereotype.Service;

@Service
public class DeviceStateService {

    private final ConcurrentMap<String, DeviceState> devices = new ConcurrentHashMap<>();
    private final CopyOnWriteArrayList<FaultEvent> faultEvents = new CopyOnWriteArrayList<>();
    private final DeviceEventService deviceEventService;

    public DeviceStateService(DeviceEventService deviceEventService) {
        this.deviceEventService = deviceEventService;
    }

    public void ingest(String deviceId, String messageType, Map<String, Object> payload, Instant receivedAt) {
        DeviceState state = devices.computeIfAbsent(deviceId, DeviceState::new);
        boolean updated = true;
        switch (messageType) {
            case "telemetry" -> state.updateTelemetry(payload, receivedAt);
            case "vision" -> state.updateVision(payload, receivedAt);
            case "fault" -> {
                state.updateFault(payload, receivedAt);
                faultEvents.add(FaultEvent.from(deviceId, receivedAt, payload));
            }
            case "commandAck" -> state.updateCommandAck(payload, receivedAt);
            case "deviceInfo" -> state.updateDeviceInfo(payload, receivedAt);
            default -> {
                // Unknown message types are intentionally ignored by the state cache.
                updated = false;
            }
        }

        if (updated) {
            DeviceStateSnapshot snapshot = state.snapshot(receivedAt);
            deviceEventService.publish(new DeviceEvent(deviceId, messageType, receivedAt, payload, snapshot));
        }
    }

    public List<DeviceStateSnapshot> findAll() {
        Instant now = Instant.now();
        return devices.values().stream()
                .map(device -> device.snapshot(now))
                .sorted(Comparator.comparing(DeviceStateSnapshot::deviceId))
                .toList();
    }

    public Optional<DeviceStateSnapshot> findById(String deviceId) {
        DeviceState state = devices.get(deviceId);
        if (state == null) {
            return Optional.empty();
        }
        return Optional.of(state.snapshot(Instant.now()));
    }

    public List<FaultEvent> findFaults(int limit) {
        return faultEvents.stream()
                .sorted(Comparator.comparing(FaultEvent::receivedAt).reversed())
                .limit(limit)
                .toList();
    }
}
