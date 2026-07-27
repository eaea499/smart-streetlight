package com.smartstreetlight.controller;

import java.util.List;

import com.smartstreetlight.dto.VisionServiceStatus;
import com.smartstreetlight.service.VisionServiceManager;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/api")
public class VisionController {

    private final VisionServiceManager visionServiceManager;

    public VisionController(VisionServiceManager visionServiceManager) {
        this.visionServiceManager = visionServiceManager;
    }

    @GetMapping("/vision/status")
    public List<VisionServiceStatus> statuses() {
        return visionServiceManager.statuses();
    }

    @GetMapping("/devices/{deviceId}/vision/status")
    public VisionServiceStatus status(@PathVariable String deviceId) {
        return visionServiceManager.status(deviceId);
    }

    @PostMapping("/devices/{deviceId}/vision/start")
    public VisionServiceStatus start(@PathVariable String deviceId) {
        return visionServiceManager.start(deviceId);
    }

    @PostMapping("/devices/{deviceId}/vision/stop")
    public VisionServiceStatus stop(@PathVariable String deviceId) {
        return visionServiceManager.stop(deviceId);
    }

    @PostMapping("/vision/stop")
    public List<VisionServiceStatus> stopAll() {
        return visionServiceManager.stopAll();
    }
}
