package com.smartstreetlight.config;

import java.util.ArrayList;
import java.util.List;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.util.StringUtils;

@ConfigurationProperties(prefix = "mqtt")
public class MqttProperties {

    private String host;
    private int port = 8883;
    private boolean tls = true;
    private String username;
    private String password;
    private String clientId = "smart-streetlight-backend";
    private boolean cleanSession = true;
    private int connectionTimeoutSeconds = 10;
    private int keepAliveSeconds = 30;
    private List<String> subscriptions = new ArrayList<>(List.of(
            "streetlight/+/telemetry",
            "streetlight/+/vision",
            "streetlight/+/fault",
            "streetlight/+/commandAck",
            "streetlight/+/deviceInfo"
    ));

    public boolean enabled() {
        return StringUtils.hasText(host);
    }

    public String brokerUri() {
        String scheme = tls ? "ssl" : "tcp";
        return scheme + "://" + host + ":" + port;
    }

    public String getHost() {
        return host;
    }

    public void setHost(String host) {
        this.host = host;
    }

    public int getPort() {
        return port;
    }

    public void setPort(int port) {
        this.port = port;
    }

    public boolean isTls() {
        return tls;
    }

    public void setTls(boolean tls) {
        this.tls = tls;
    }

    public String getUsername() {
        return username;
    }

    public void setUsername(String username) {
        this.username = username;
    }

    public String getPassword() {
        return password;
    }

    public void setPassword(String password) {
        this.password = password;
    }

    public String getClientId() {
        return clientId;
    }

    public void setClientId(String clientId) {
        this.clientId = clientId;
    }

    public boolean isCleanSession() {
        return cleanSession;
    }

    public void setCleanSession(boolean cleanSession) {
        this.cleanSession = cleanSession;
    }

    public int getConnectionTimeoutSeconds() {
        return connectionTimeoutSeconds;
    }

    public void setConnectionTimeoutSeconds(int connectionTimeoutSeconds) {
        this.connectionTimeoutSeconds = connectionTimeoutSeconds;
    }

    public int getKeepAliveSeconds() {
        return keepAliveSeconds;
    }

    public void setKeepAliveSeconds(int keepAliveSeconds) {
        this.keepAliveSeconds = keepAliveSeconds;
    }

    public List<String> getSubscriptions() {
        return subscriptions;
    }

    public void setSubscriptions(List<String> subscriptions) {
        this.subscriptions = subscriptions;
    }
}
