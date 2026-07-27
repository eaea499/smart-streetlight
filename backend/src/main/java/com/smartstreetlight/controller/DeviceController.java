package com.smartstreetlight.controller;

import java.util.ArrayList;
import java.util.List;

import com.smartstreetlight.dto.BatchCommandPublishResponse;
import com.smartstreetlight.dto.BatchCommandRequest;
import com.smartstreetlight.dto.CommandPublishResponse;
import com.smartstreetlight.dto.CommandRequest;
import com.smartstreetlight.model.DeviceStateSnapshot;
import com.smartstreetlight.model.FaultEvent;
import com.smartstreetlight.mqtt.MqttGateway;
import com.smartstreetlight.service.CommandValidator;
import com.smartstreetlight.service.DeviceStateService;
import jakarta.validation.Valid;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.server.ResponseStatusException;

import static org.springframework.http.HttpStatus.BAD_REQUEST;
import static org.springframework.http.HttpStatus.NOT_FOUND;

@RestController
@RequestMapping("/api")
public class DeviceController {

    private final DeviceStateService deviceStateService;
    private final MqttGateway mqttGateway;
    private final CommandValidator commandValidator;

    public DeviceController(DeviceStateService deviceStateService, MqttGateway mqttGateway, CommandValidator commandValidator) {
        this.deviceStateService = deviceStateService;
        this.mqttGateway = mqttGateway;
        this.commandValidator = commandValidator;
    }

    @GetMapping("/mqtt/status")
    public MqttStatus mqttStatus() {
        return new MqttStatus(mqttGateway.isConnected());
    }

    @GetMapping("/devices")
    public List<DeviceStateSnapshot> listDevices() {
        return deviceStateService.findAll();
    }

    @GetMapping("/devices/{deviceId}")
    public DeviceStateSnapshot getDevice(@PathVariable String deviceId) {
        return deviceStateService.findById(deviceId)
                .orElseThrow(() -> new ResponseStatusException(NOT_FOUND, "device not found"));
    }

    @PostMapping("/devices/{deviceId}/command")
    public CommandPublishResponse publishDeviceCommand(
            @PathVariable String deviceId,
            @Valid @RequestBody CommandRequest request
    ) {
        commandValidator.validate(request);
        return mqttGateway.publishDeviceCommand(deviceId, request);
    }

    @PostMapping("/devices/batch-command")
    public BatchCommandPublishResponse publishBatchCommand(@Valid @RequestBody BatchCommandRequest request) {
        CommandRequest command = request.toCommandRequest();
        commandValidator.validate(command);

        boolean broadcast = Boolean.TRUE.equals(request.broadcast());
        List<String> deviceIds = request.deviceIds() == null ? List.of() : request.deviceIds().stream()
                .filter(id -> id != null && !id.isBlank())
                .map(String::trim)
                .distinct()
                .toList();

        List<CommandPublishResponse> results = new ArrayList<>();
        if (broadcast || deviceIds.isEmpty()) {
            results.add(mqttGateway.publishBroadcastCommand(command));
        } else {
            deviceIds.forEach(deviceId -> results.add(mqttGateway.publishDeviceCommand(deviceId, command)));
        }
        return new BatchCommandPublishResponse(results);
    }

    @GetMapping("/faults")
    public List<FaultEvent> listFaults(@RequestParam(defaultValue = "100") int limit) {
        if (limit <= 0 || limit > 500) {
            throw new ResponseStatusException(BAD_REQUEST, "limit must be between 1 and 500");
        }
        return deviceStateService.findFaults(limit);
    }

    public record MqttStatus(boolean connected) {
    }
}
