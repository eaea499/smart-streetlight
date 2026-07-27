package com.smartstreetlight.mqtt;

import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.Arrays;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.smartstreetlight.config.MqttProperties;
import com.smartstreetlight.dto.CommandPublishResponse;
import com.smartstreetlight.dto.CommandRequest;
import com.smartstreetlight.service.DeviceStateService;
import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken;
import org.eclipse.paho.client.mqttv3.MqttCallbackExtended;
import org.eclipse.paho.client.mqttv3.MqttClient;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.SmartLifecycle;
import org.springframework.stereotype.Service;
import org.springframework.util.StringUtils;
import org.springframework.web.server.ResponseStatusException;

import static org.springframework.http.HttpStatus.SERVICE_UNAVAILABLE;

@Service
public class MqttGateway implements SmartLifecycle {

    private static final Logger log = LoggerFactory.getLogger(MqttGateway.class);
    private static final TypeReference<Map<String, Object>> MAP_TYPE = new TypeReference<>() {
    };

    private final MqttProperties properties;
    private final DeviceStateService deviceStateService;
    private final ObjectMapper objectMapper;
    private final AtomicBoolean running = new AtomicBoolean(false);
    private volatile MqttClient client;

    public MqttGateway(MqttProperties properties, DeviceStateService deviceStateService, ObjectMapper objectMapper) {
        this.properties = properties;
        this.deviceStateService = deviceStateService;
        this.objectMapper = objectMapper;
    }

    @Override
    public void start() {
        if (!properties.enabled()) {
            log.warn("MQTT is disabled because mqtt.host is empty. REST APIs will start, but command publishing is unavailable.");
            return;
        }
        try {
            String clientId = properties.getClientId() + "-" + UUID.randomUUID().toString().substring(0, 8);
            MqttClient mqttClient = new MqttClient(properties.brokerUri(), clientId, new MemoryPersistence());
            mqttClient.setCallback(new BackendMqttCallback());
            mqttClient.connect(buildConnectOptions());
            client = mqttClient;
            running.set(true);
            subscribeAll();
            log.info("Connected to MQTT broker {}", properties.brokerUri());
        } catch (MqttException ex) {
            running.set(false);
            log.error("Failed to connect to MQTT broker {}", properties.brokerUri(), ex);
        }
    }

    @Override
    public void stop() {
        MqttClient mqttClient = client;
        if (mqttClient == null) {
            running.set(false);
            return;
        }
        try {
            if (mqttClient.isConnected()) {
                mqttClient.disconnect();
            }
            mqttClient.close();
        } catch (MqttException ex) {
            log.warn("Failed to stop MQTT client cleanly", ex);
        } finally {
            running.set(false);
            client = null;
        }
    }

    @Override
    public boolean isRunning() {
        MqttClient mqttClient = client;
        return running.get() && mqttClient != null && mqttClient.isConnected();
    }

    public CommandPublishResponse publishDeviceCommand(String deviceId, CommandRequest request) {
        return publishCommand("streetlight/" + deviceId + "/command", request);
    }

    public CommandPublishResponse publishBroadcastCommand(CommandRequest request) {
        return publishCommand("streetlight/all/command", request);
    }

    public boolean isConnected() {
        return isRunning();
    }

    private CommandPublishResponse publishCommand(String topic, CommandRequest request) {
        MqttClient mqttClient = client;
        if (mqttClient == null || !mqttClient.isConnected()) {
            throw new ResponseStatusException(SERVICE_UNAVAILABLE, "MQTT broker is not connected");
        }
        Map<String, Object> payload = request.toPayload();
        try {
            byte[] body = objectMapper.writeValueAsBytes(payload);
            mqttClient.publish(topic, body, 0, false);
            return new CommandPublishResponse(topic, payload, Instant.now());
        } catch (Exception ex) {
            throw new ResponseStatusException(SERVICE_UNAVAILABLE, "Failed to publish MQTT command", ex);
        }
    }

    private MqttConnectOptions buildConnectOptions() {
        MqttConnectOptions options = new MqttConnectOptions();
        options.setAutomaticReconnect(true);
        options.setCleanSession(properties.isCleanSession());
        options.setConnectionTimeout(properties.getConnectionTimeoutSeconds());
        options.setKeepAliveInterval(properties.getKeepAliveSeconds());
        if (StringUtils.hasText(properties.getUsername())) {
            options.setUserName(properties.getUsername());
        }
        if (StringUtils.hasText(properties.getPassword())) {
            options.setPassword(properties.getPassword().toCharArray());
        }
        return options;
    }

    private void subscribeAll() {
        MqttClient mqttClient = client;
        if (mqttClient == null || !mqttClient.isConnected() || properties.getSubscriptions().isEmpty()) {
            return;
        }
        try {
            String[] topics = properties.getSubscriptions().toArray(String[]::new);
            int[] qos = new int[topics.length];
            Arrays.fill(qos, 0);
            mqttClient.subscribe(topics, qos);
            log.info("Subscribed MQTT topics: {}", String.join(", ", topics));
        } catch (MqttException ex) {
            log.error("Failed to subscribe MQTT topics", ex);
        }
    }

    private void handleMessage(String topic, MqttMessage message) {
        TopicParts parts = TopicParts.parse(topic);
        if (parts == null) {
            log.debug("Ignored MQTT topic {}", topic);
            return;
        }
        try {
            String json = new String(message.getPayload(), StandardCharsets.UTF_8);
            Map<String, Object> payload = objectMapper.readValue(json, MAP_TYPE);
            deviceStateService.ingest(parts.deviceId(), parts.messageType(), payload, Instant.now());
        } catch (Exception ex) {
            log.warn("Failed to parse MQTT message from topic {}", topic, ex);
        }
    }

    private class BackendMqttCallback implements MqttCallbackExtended {
        @Override
        public void connectComplete(boolean reconnect, String serverURI) {
            running.set(true);
            log.info("MQTT {}connected to {}", reconnect ? "re" : "", serverURI);
            subscribeAll();
        }

        @Override
        public void connectionLost(Throwable cause) {
            running.set(false);
            log.warn("MQTT connection lost", cause);
        }

        @Override
        public void messageArrived(String topic, MqttMessage message) {
            handleMessage(topic, message);
        }

        @Override
        public void deliveryComplete(IMqttDeliveryToken token) {
            // QoS 0 commands do not need extra handling here.
        }
    }

    private record TopicParts(String deviceId, String messageType) {
        static TopicParts parse(String topic) {
            String[] parts = topic.split("/");
            if (parts.length != 3 || !"streetlight".equals(parts[0]) || "all".equals(parts[1])) {
                return null;
            }
            return new TopicParts(parts[1], parts[2]);
        }
    }
}
