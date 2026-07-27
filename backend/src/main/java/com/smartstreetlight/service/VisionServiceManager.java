package com.smartstreetlight.service;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.time.Instant;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import com.smartstreetlight.config.MqttProperties;
import com.smartstreetlight.config.VisionProperties;
import com.smartstreetlight.dto.VisionServiceStatus;
import com.smartstreetlight.model.DeviceStateSnapshot;
import jakarta.annotation.PreDestroy;
import org.springframework.stereotype.Service;
import org.springframework.util.StringUtils;
import org.springframework.web.server.ResponseStatusException;

import static org.springframework.http.HttpStatus.BAD_REQUEST;
import static org.springframework.http.HttpStatus.CONFLICT;
import static org.springframework.http.HttpStatus.NOT_FOUND;
import static org.springframework.http.HttpStatus.SERVICE_UNAVAILABLE;

@Service
public class VisionServiceManager {

    private static final int MAX_LOG_LINES = 40;

    private final VisionProperties visionProperties;
    private final MqttProperties mqttProperties;
    private final DeviceStateService deviceStateService;
    private final Map<String, VisionProcessState> states = new java.util.LinkedHashMap<>();

    public VisionServiceManager(
            VisionProperties visionProperties,
            MqttProperties mqttProperties,
            DeviceStateService deviceStateService
    ) {
        this.visionProperties = visionProperties;
        this.mqttProperties = mqttProperties;
        this.deviceStateService = deviceStateService;
    }

    public synchronized VisionServiceStatus start(String requestedDeviceId) {
        VisionProcessState state = stateFor(requestedDeviceId);
        refreshProcessState(state);
        if (state.process != null && state.process.isAlive()) {
            throw new ResponseStatusException(CONFLICT, "YOLO service is already running for device " + requestedDeviceId);
        }
        if (!mqttProperties.enabled()) {
            throw new ResponseStatusException(SERVICE_UNAVAILABLE, "MQTT settings are required before starting YOLO service");
        }

        DeviceStateSnapshot device = deviceStateService.findById(requestedDeviceId)
                .orElseThrow(() -> new ResponseStatusException(NOT_FOUND, "device not found"));
        String cameraBaseUrl = findCameraBaseUrl(device.latestDeviceInfo());
        if (!StringUtils.hasText(cameraBaseUrl)) {
            throw new ResponseStatusException(BAD_REQUEST, "deviceInfo.cameraBaseUrl is missing");
        }

        String streamSource = cameraBaseUrl.replaceAll("/+$", "") + "/stream";
        List<String> command = buildCommand(requestedDeviceId, streamSource);

        try {
            ProcessBuilder processBuilder = new ProcessBuilder(command);
            processBuilder.directory(resolveWorkingDirectory().toFile());
            processBuilder.redirectErrorStream(true);
            processBuilder.environment().put("PYTHONUTF8", "1");
            processBuilder.environment().put("PYTHONUNBUFFERED", "1");

            state.process = processBuilder.start();
            state.source = streamSource;
            state.startedAt = Instant.now();
            state.stoppedAt = null;
            state.exitCode = null;
            state.state = "running";
            state.message = "YOLO 服务已启动";
            state.recentLogs.clear();
            addLog(state, "started YOLO service, source=" + streamSource);
            startLogReader(state, state.process);
            return statusFor(state);
        } catch (IOException ex) {
            state.process = null;
            state.stoppedAt = Instant.now();
            state.exitCode = null;
            state.state = "failed";
            state.message = "启动 YOLO 服务失败：" + ex.getMessage();
            addLog(state, state.message);
            throw new ResponseStatusException(SERVICE_UNAVAILABLE, state.message, ex);
        }
    }

    public synchronized VisionServiceStatus stop(String requestedDeviceId) {
        VisionProcessState state = stateFor(requestedDeviceId);
        return stopState(state);
    }

    public synchronized List<VisionServiceStatus> stopAll() {
        Set<String> deviceIds = knownDeviceIds();
        if (deviceIds.isEmpty()) {
            deviceIds.addAll(states.keySet());
        }

        List<VisionServiceStatus> result = new ArrayList<>();
        for (String deviceId : deviceIds) {
            result.add(stopState(stateFor(deviceId)));
        }
        return result;
    }

    public synchronized VisionServiceStatus status(String requestedDeviceId) {
        return statusFor(stateFor(requestedDeviceId));
    }

    public synchronized List<VisionServiceStatus> statuses() {
        Set<String> deviceIds = knownDeviceIds();
        deviceIds.addAll(states.keySet());

        List<VisionServiceStatus> result = new ArrayList<>();
        for (String deviceId : deviceIds) {
            result.add(statusFor(stateFor(deviceId)));
        }
        return result;
    }

    @PreDestroy
    public void shutdown() {
        stopAll();
    }

    private VisionServiceStatus stopState(VisionProcessState state) {
        refreshProcessState(state);
        if (state.process == null || !state.process.isAlive()) {
            state.state = "stopped";
            state.message = "YOLO 服务未运行";
            return statusFor(state);
        }

        state.process.destroy();
        try {
            if (!state.process.waitFor(3, java.util.concurrent.TimeUnit.SECONDS)) {
                state.process.destroyForcibly();
                state.process.waitFor(3, java.util.concurrent.TimeUnit.SECONDS);
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
        }
        refreshProcessState(state);
        if (!"exited".equals(state.state)) {
            state.state = "stopped";
            state.message = "YOLO 服务已停止";
        }
        return statusFor(state);
    }

    private VisionServiceStatus statusFor(VisionProcessState state) {
        refreshProcessState(state);
        return new VisionServiceStatus(
                state.process != null && state.process.isAlive(),
                state.state,
                state.deviceId,
                state.source,
                state.startedAt,
                state.stoppedAt,
                state.exitCode,
                state.message,
                List.copyOf(state.recentLogs)
        );
    }

    private void refreshProcessState(VisionProcessState state) {
        if (state.process != null && !state.process.isAlive() && state.exitCode == null) {
            state.exitCode = state.process.exitValue();
            state.stoppedAt = Instant.now();
            state.state = state.exitCode == 0 ? "stopped" : "exited";
            state.message = state.exitCode == 0 ? "YOLO 服务已停止" : "YOLO 服务已退出，退出码 " + state.exitCode;
            addLog(state, state.message);
        }
    }

    private void startLogReader(VisionProcessState state, Process startedProcess) {
        Thread thread = new Thread(() -> {
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(startedProcess.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    synchronized (VisionServiceManager.this) {
                        addLog(state, line);
                    }
                }
            } catch (IOException ex) {
                synchronized (VisionServiceManager.this) {
                    addLog(state, "failed to read YOLO service output: " + ex.getMessage());
                }
            }
        }, "vision-service-log-reader-" + state.deviceId);
        thread.setDaemon(true);
        thread.start();
    }

    private void addLog(VisionProcessState state, String line) {
        if (!StringUtils.hasText(line)) {
            return;
        }
        state.recentLogs.addLast(line);
        while (state.recentLogs.size() > MAX_LOG_LINES) {
            state.recentLogs.removeFirst();
        }
    }

    private List<String> buildCommand(String targetDeviceId, String streamSource) {
        List<String> command = new ArrayList<>();
        if (StringUtils.hasText(visionProperties.getCondaEnv())) {
            command.add(visionProperties.getCondaCommand());
            command.add("run");
            command.add("--no-capture-output");
            command.add("-n");
            command.add(visionProperties.getCondaEnv());
            command.add("python");
        } else {
            command.add(visionProperties.getPythonCommand());
        }

        command.add("-u");
        command.add("-m");
        command.add("vision.vision_service");
        command.add("--source");
        command.add(streamSource);
        command.add("--device-id");
        command.add(targetDeviceId);
        command.add("--model");
        command.add(visionProperties.getModel());
        command.add("--conf");
        command.add(Double.toString(visionProperties.getConfidence()));
        command.add("--frame-stride");
        command.add(Integer.toString(visionProperties.getFrameStride()));
        command.add("--publish-interval-ms");
        command.add(Integer.toString(visionProperties.getPublishIntervalMs()));
        command.add("--stable-window");
        command.add(Integer.toString(visionProperties.getStableWindow()));
        command.add("--occupied-min-hits");
        command.add(Integer.toString(visionProperties.getOccupiedMinHits()));
        command.add("--empty-min-hits");
        command.add(Integer.toString(visionProperties.getEmptyMinHits()));
        if (visionProperties.isAutoPauseByDark()) {
            command.add("--auto-pause-by-dark");
        }
        if (visionProperties.isShowWindow()) {
            command.add("--show");
        }

        command.add("--host");
        command.add(mqttProperties.getHost());
        command.add("--port");
        command.add(Integer.toString(mqttProperties.getPort()));
        if (mqttProperties.isTls()) {
            command.add("--tls");
        }
        if (StringUtils.hasText(mqttProperties.getUsername())) {
            command.add("--username");
            command.add(mqttProperties.getUsername());
        }
        if (StringUtils.hasText(mqttProperties.getPassword())) {
            command.add("--password");
            command.add(mqttProperties.getPassword());
        }
        command.add("--client-id");
        command.add("vision_service_" + targetDeviceId.replaceAll("[^A-Za-z0-9_]", "_"));
        return command;
    }

    private Path resolveWorkingDirectory() {
        return Path.of(visionProperties.getWorkingDirectory()).toAbsolutePath().normalize();
    }

    private VisionProcessState stateFor(String requestedDeviceId) {
        return states.computeIfAbsent(requestedDeviceId, VisionProcessState::new);
    }

    private Set<String> knownDeviceIds() {
        Set<String> deviceIds = new LinkedHashSet<>();
        for (DeviceStateSnapshot device : deviceStateService.findAll()) {
            deviceIds.add(device.deviceId());
        }
        return deviceIds;
    }

    private static String findCameraBaseUrl(Map<String, Object> deviceInfo) {
        Object value = deviceInfo == null ? null : deviceInfo.get("cameraBaseUrl");
        return value instanceof String text ? text : "";
    }

    private static class VisionProcessState {
        private final String deviceId;
        private final ArrayDeque<String> recentLogs = new ArrayDeque<>();

        private Process process;
        private String source;
        private Instant startedAt;
        private Instant stoppedAt;
        private Integer exitCode;
        private String state = "stopped";
        private String message = "YOLO 服务未启动";

        private VisionProcessState(String deviceId) {
            this.deviceId = deviceId;
        }
    }
}
