package com.smartstreetlight.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties(prefix = "vision")
public class VisionProperties {

    private String workingDirectory = "..";
    private String pythonCommand = "python";
    private String condaCommand = "conda";
    private String condaEnv = "";
    private String model = "yolov8n.pt";
    private double confidence = 0.35;
    private int frameStride = 15;
    private int publishIntervalMs = 2000;
    private int stableWindow = 5;
    private int occupiedMinHits = 2;
    private int emptyMinHits = 4;
    private boolean autoPauseByDark = true;
    private boolean showWindow = false;

    public String getWorkingDirectory() {
        return workingDirectory;
    }

    public void setWorkingDirectory(String workingDirectory) {
        this.workingDirectory = workingDirectory;
    }

    public String getPythonCommand() {
        return pythonCommand;
    }

    public void setPythonCommand(String pythonCommand) {
        this.pythonCommand = pythonCommand;
    }

    public String getCondaCommand() {
        return condaCommand;
    }

    public void setCondaCommand(String condaCommand) {
        this.condaCommand = condaCommand;
    }

    public String getCondaEnv() {
        return condaEnv;
    }

    public void setCondaEnv(String condaEnv) {
        this.condaEnv = condaEnv;
    }

    public String getModel() {
        return model;
    }

    public void setModel(String model) {
        this.model = model;
    }

    public double getConfidence() {
        return confidence;
    }

    public void setConfidence(double confidence) {
        this.confidence = confidence;
    }

    public int getFrameStride() {
        return frameStride;
    }

    public void setFrameStride(int frameStride) {
        this.frameStride = frameStride;
    }

    public int getPublishIntervalMs() {
        return publishIntervalMs;
    }

    public void setPublishIntervalMs(int publishIntervalMs) {
        this.publishIntervalMs = publishIntervalMs;
    }

    public int getStableWindow() {
        return stableWindow;
    }

    public void setStableWindow(int stableWindow) {
        this.stableWindow = stableWindow;
    }

    public int getOccupiedMinHits() {
        return occupiedMinHits;
    }

    public void setOccupiedMinHits(int occupiedMinHits) {
        this.occupiedMinHits = occupiedMinHits;
    }

    public int getEmptyMinHits() {
        return emptyMinHits;
    }

    public void setEmptyMinHits(int emptyMinHits) {
        this.emptyMinHits = emptyMinHits;
    }

    public boolean isAutoPauseByDark() {
        return autoPauseByDark;
    }

    public void setAutoPauseByDark(boolean autoPauseByDark) {
        this.autoPauseByDark = autoPauseByDark;
    }

    public boolean isShowWindow() {
        return showWindow;
    }

    public void setShowWindow(boolean showWindow) {
        this.showWindow = showWindow;
    }
}
