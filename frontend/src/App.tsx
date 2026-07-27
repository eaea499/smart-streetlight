import { useEffect, useMemo, useState } from "react";
import {
  Activity,
  AlertTriangle,
  Camera,
  CheckCircle2,
  Clock3,
  ExternalLink,
  Gauge,
  Lightbulb,
  Loader2,
  Moon,
  Play,
  Power,
  Radio,
  RefreshCw,
  RotateCcw,
  Send,
  SlidersHorizontal,
  Square,
  Sun,
  Users,
  Video,
  Wifi,
  WifiOff,
  Zap,
} from "lucide-react";
import {
  API_BASE,
  CAMERA_BASE,
  CommandPayload,
  DeviceEvent,
  DeviceStateSnapshot,
  FaultEvent,
  getDevices,
  getFaults,
  getMqttStatus,
  getVisionServiceStatuses,
  publishBroadcastCommand,
  publishDeviceCommand,
  startVisionService,
  stopVisionService,
  VisionServiceStatus,
} from "./api";

type SseState = "connecting" | "connected" | "reconnecting" | "closed";
const MAX_EVENT_LOG_ITEMS = 8;

interface EventLogItem {
  id: string;
  messageType: string;
  deviceId: string;
  receivedAt: string;
}

function asNumber(value: unknown, fallback = 0): number {
  return typeof value === "number" && Number.isFinite(value) ? value : fallback;
}

function asBoolean(value: unknown, fallback = false): boolean {
  return typeof value === "boolean" ? value : fallback;
}

function asString(value: unknown, fallback = ""): string {
  return typeof value === "string" ? value : fallback;
}

function normalizeCameraBaseUrl(value: string): string {
  const trimmed = value.trim();
  if (!trimmed) {
    return "";
  }
  const withProtocol = trimmed.startsWith("http://") || trimmed.startsWith("https://") ? trimmed : `http://${trimmed}`;
  return withProtocol.replace(/\/(stream|capture)\/?$/i, "").replace(/\/$/, "");
}

function getCameraBaseUrl(
  deviceInfo: Record<string, unknown>,
  telemetry: Record<string, unknown>,
  latestVision: Record<string, unknown>,
): string {
  const deviceInfoCameraBase = normalizeCameraBaseUrl(asString(deviceInfo.cameraBaseUrl));
  if (deviceInfoCameraBase) {
    return deviceInfoCameraBase;
  }

  const telemetryCameraBase = normalizeCameraBaseUrl(asString(telemetry.cameraBaseUrl));
  if (telemetryCameraBase) {
    return telemetryCameraBase;
  }

  const telemetryIp = normalizeCameraBaseUrl(asString(telemetry.ipAddress));
  if (telemetryIp) {
    return telemetryIp;
  }

  const visionSource = normalizeCameraBaseUrl(asString(latestVision.source));
  if (visionSource) {
    return visionSource;
  }

  return CAMERA_BASE;
}

function faultLabel(value: string | null | undefined): string {
  switch (value) {
    case "none":
      return "故障已恢复";
    case "open_load":
      return "断路 / LED 异常";
    case "ina219_offline":
      return "电流传感器离线";
    case "unexpected_current":
      return "异常电流";
    case "over_current":
      return "电流过大";
    default:
      return value || "-";
  }
}

function faultMessage(value: string | null | undefined): string {
  switch (value) {
    case "streetlight may be disconnected or LED failed":
      return "路灯可能断开或 LED 损坏";
    case "fault cleared":
      return "故障已清除";
    case "INA219 current sensor is offline":
      return "INA219 电流传感器离线";
    case "unexpected current detected while streetlight is off":
      return "关灯状态下检测到异常电流";
    case "streetlight current is too high":
      return "路灯电流过大";
    default:
      return value || "-";
  }
}

function formatTime(value: string | null): string {
  if (!value) {
    return "-";
  }
  return new Date(value).toLocaleTimeString("zh-CN", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function formatDateTime(value: string | null): string {
  if (!value) {
    return "-";
  }
  return new Date(value).toLocaleString("zh-CN", {
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function updateDeviceList(devices: DeviceStateSnapshot[], next: DeviceStateSnapshot): DeviceStateSnapshot[] {
  const exists = devices.some((device) => device.deviceId === next.deviceId);
  if (!exists) {
    return [...devices, next].sort((a, b) => a.deviceId.localeCompare(b.deviceId));
  }
  return devices.map((device) => (device.deviceId === next.deviceId ? next : device));
}

function StatTile({
  icon,
  label,
  value,
  tone = "default",
}: {
  icon: React.ReactNode;
  label: string;
  value: string;
  tone?: "default" | "good" | "warn" | "danger" | "info";
}) {
  return (
    <div className={`stat-tile stat-${tone}`}>
      <div className="stat-icon">{icon}</div>
      <div>
        <div className="stat-label">{label}</div>
        <div className="stat-value">{value}</div>
      </div>
    </div>
  );
}

function StatusPill({
  children,
  tone = "neutral",
}: {
  children: React.ReactNode;
  tone?: "neutral" | "good" | "warn" | "danger" | "info";
}) {
  return <span className={`status-pill status-${tone}`}>{children}</span>;
}

function getYoloStatus(dark: boolean, visionValid: boolean, online: boolean) {
  if (!online) {
    return {
      label: "设备离线",
      description: "等待设备恢复在线",
      pillTone: "danger" as const,
      statTone: "danger" as const,
    };
  }
  if (!dark) {
    return {
      label: "白天暂停",
      description: "白天不读取视频流",
      pillTone: "neutral" as const,
      statTone: "default" as const,
    };
  }
  if (visionValid) {
    return {
      label: "YOLO 检测中",
      description: "夜晚视觉结果有效",
      pillTone: "good" as const,
      statTone: "good" as const,
    };
  }
  return {
    label: "等待更新",
    description: "未收到有效视觉结果",
    pillTone: "warn" as const,
    statTone: "warn" as const,
  };
}

function getVisionProcessLabel(status: VisionServiceStatus | null): string {
  if (!status) {
    return "状态未知";
  }
  if (status.running) {
    return "后端运行中";
  }
  if (status.state === "failed") {
    return "启动失败";
  }
  if (status.state === "exited") {
    return "异常退出";
  }
  return "后端未运行";
}

function getVisionProcessTone(status: VisionServiceStatus | null): "neutral" | "good" | "warn" | "danger" {
  if (!status) {
    return "neutral";
  }
  if (status.running) {
    return "good";
  }
  if (status.state === "failed" || status.state === "exited") {
    return "danger";
  }
  return "neutral";
}

function App() {
  const [devices, setDevices] = useState<DeviceStateSnapshot[]>([]);
  const [faults, setFaults] = useState<FaultEvent[]>([]);
  const [selectedDeviceId, setSelectedDeviceId] = useState("GLG-A-001");
  const [mqttConnected, setMqttConnected] = useState(false);
  const [sseState, setSseState] = useState<SseState>("connecting");
  const [brightness, setBrightness] = useState(59);
  const [timeoutMode, setTimeoutMode] = useState("default");
  const [busyAction, setBusyAction] = useState<string | null>(null);
  const [feedback, setFeedback] = useState("");
  const [error, setError] = useState("");
  const [visionFeedback, setVisionFeedback] = useState("");
  const [visionError, setVisionError] = useState("");
  const [visionServiceStatuses, setVisionServiceStatuses] = useState<Record<string, VisionServiceStatus>>({});
  const [eventLog, setEventLog] = useState<EventLogItem[]>([]);

  const selectedDevice = useMemo(() => {
    return devices.find((device) => device.deviceId === selectedDeviceId) ?? devices[0] ?? null;
  }, [devices, selectedDeviceId]);

  const telemetry = selectedDevice?.latestTelemetry ?? {};
  const commandAck = selectedDevice?.latestCommandAck ?? {};
  const latestFault = selectedDevice?.latestFault ?? {};
  const latestDeviceInfo = selectedDevice?.latestDeviceInfo ?? {};
  const visionServiceStatus = selectedDevice ? visionServiceStatuses[selectedDevice.deviceId] ?? null : null;
  const currentBrightness = asNumber(telemetry.brightnessPercent);
  const mode = asString(telemetry.mode, "-");
  const dark = asBoolean(telemetry.dark);
  const occupied = asBoolean(telemetry.occupied);
  const visionValid = asBoolean(telemetry.visionValid);
  const online = selectedDevice?.online ?? false;
  const latestVision = selectedDevice?.latestVision ?? {};
  const peopleCount = asNumber(telemetry.peopleCount, asNumber(latestVision.peopleCount));
  const visionConfidence = asNumber(telemetry.visionConfidence, asNumber(latestVision.maxConfidence));
  const cameraBase = getCameraBaseUrl(latestDeviceInfo, telemetry, latestVision);
  const yoloStatus = getYoloStatus(dark, visionValid, online);
  const visionProcessTone = getVisionProcessTone(visionServiceStatus);
  const visionLogs = visionServiceStatus?.recentLogs ?? [];
  const latestVisionLog = visionLogs.length > 0 ? visionLogs[visionLogs.length - 1] : "";
  const fault = asString(telemetry.fault || latestFault.fault, "none");

  function openCameraPath(path: string) {
    window.open(`${cameraBase}${path}`, "_blank", "noopener,noreferrer");
  }

  useEffect(() => {
    let cancelled = false;

    async function loadInitialData() {
      try {
        const [status, deviceList, faultList] = await Promise.all([getMqttStatus(), getDevices(), getFaults()]);
        if (cancelled) {
          return;
        }
        setMqttConnected(status.connected);
        setDevices(deviceList);
        setFaults(faultList);
        if (deviceList.length > 0) {
          setSelectedDeviceId((current) => (deviceList.some((device) => device.deviceId === current) ? current : deviceList[0].deviceId));
        }
      } catch (ex) {
        if (!cancelled) {
          setError(ex instanceof Error ? ex.message : "后端连接失败");
        }
      }
    }

    loadInitialData();
    const timer = window.setInterval(() => {
      getMqttStatus()
        .then((status) => setMqttConnected(status.connected))
        .catch(() => setMqttConnected(false));
    }, 5000);

    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, []);

  useEffect(() => {
    const source = new EventSource(`${API_BASE}/api/events/stream`);
    setSseState("connecting");

    source.addEventListener("connected", () => setSseState("connected"));
    source.addEventListener("device-state", (event) => {
      const data = JSON.parse((event as MessageEvent).data) as DeviceEvent;
      setDevices((current) => updateDeviceList(current, data.device));
      setSelectedDeviceId((current) => current || data.deviceId);
      setEventLog((current) => {
        const nextEvent = {
          id: `${data.receivedAt}-${data.deviceId}-${data.messageType}`,
          messageType: data.messageType,
          deviceId: data.deviceId,
          receivedAt: data.receivedAt,
        };

        return [nextEvent, ...current.filter((item) => item.id !== nextEvent.id)].slice(0, MAX_EVENT_LOG_ITEMS);
      });
      if (data.messageType === "fault") {
        getFaults().then(setFaults).catch(() => undefined);
      }
    });
    source.onerror = () => setSseState("reconnecting");

    return () => {
      source.close();
      setSseState("closed");
    };
  }, []);

  useEffect(() => {
    let cancelled = false;

    function refreshVisionServiceStatus() {
      getVisionServiceStatuses()
        .then((statuses) => {
          if (!cancelled) {
            applyVisionStatuses(statuses);
          }
        })
        .catch(() => {
          if (!cancelled) {
            setVisionServiceStatuses({});
          }
        });
    }

    refreshVisionServiceStatus();
    const timer = window.setInterval(refreshVisionServiceStatus, 3000);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, []);

  useEffect(() => {
    if (currentBrightness >= 0 && currentBrightness <= 100) {
      setBrightness(currentBrightness || 59);
    }
  }, [selectedDeviceId]);

  async function runCommand(action: string, payload: CommandPayload) {
    if (!selectedDevice) {
      setError("没有可控制的设备");
      return;
    }

    setBusyAction(action);
    setError("");
    setFeedback("");
    try {
      const response = await publishDeviceCommand(selectedDevice.deviceId, payload);
      setFeedback(`已发布到 ${response.topic}`);
    } catch (ex) {
      setError(ex instanceof Error ? ex.message : "命令发布失败");
    } finally {
      setBusyAction(null);
    }
  }

  async function runBroadcastAuto() {
    setBusyAction("broadcast-auto");
    setError("");
    setFeedback("");
    try {
      const response = await publishBroadcastCommand({ mode: "auto" });
      setFeedback(`广播已发布到 ${response.results[0]?.topic ?? "streetlight/all/command"}`);
    } catch (ex) {
      setError(ex instanceof Error ? ex.message : "广播命令失败");
    } finally {
      setBusyAction(null);
    }
  }

  function applyVisionStatuses(statuses: VisionServiceStatus[]) {
    setVisionServiceStatuses(
      Object.fromEntries(statuses.filter((status) => status.deviceId).map((status) => [status.deviceId as string, status])),
    );
  }

  function rememberVisionStatus(status: VisionServiceStatus) {
    if (!status.deviceId) {
      return;
    }
    setVisionServiceStatuses((current) => ({
      ...current,
      [status.deviceId as string]: status,
    }));
  }

  async function runVisionStart() {
    if (!selectedDevice) {
      setVisionError("没有可检测的设备");
      return;
    }

    setBusyAction("vision-start");
    setVisionError("");
    setVisionFeedback("");
    try {
      const status = await startVisionService(selectedDevice.deviceId);
      rememberVisionStatus(status);
      setVisionFeedback(`${selectedDevice.deviceId}：${status.message || "YOLO 服务启动命令已发送"}`);
    } catch (ex) {
      setVisionError(ex instanceof Error ? ex.message : "YOLO 服务启动失败");
      getVisionServiceStatuses().then(applyVisionStatuses).catch(() => undefined);
    } finally {
      setBusyAction(null);
    }
  }

  async function runVisionStop() {
    if (!selectedDevice) {
      setVisionError("没有可停止的设备");
      return;
    }

    setBusyAction("vision-stop");
    setVisionError("");
    setVisionFeedback("");
    try {
      const status = await stopVisionService(selectedDevice.deviceId);
      rememberVisionStatus(status);
      setVisionFeedback(`${selectedDevice.deviceId}：${status.message || "YOLO 服务已停止"}`);
    } catch (ex) {
      setVisionError(ex instanceof Error ? ex.message : "YOLO 服务停止失败");
      getVisionServiceStatuses().then(applyVisionStatuses).catch(() => undefined);
    } finally {
      setBusyAction(null);
    }
  }

  function manualPayload(level: number): CommandPayload {
    const payload: CommandPayload = {
      mode: "manual",
      brightnessPercent: level,
    };
    if (timeoutMode !== "default") {
      payload.manualTimeoutMs = Number(timeoutMode);
    }
    return payload;
  }

  return (
    <main className="app-shell">
      <header className="topbar">
        <div>
          <div className="eyebrow">Smart Streetlight</div>
          <h1>智能路灯管控台</h1>
        </div>
        <div className="topbar-status">
          <StatusPill tone={mqttConnected ? "good" : "danger"}>
            {mqttConnected ? <Wifi size={16} /> : <WifiOff size={16} />}
            MQTT {mqttConnected ? "已连接" : "未连接"}
          </StatusPill>
          <StatusPill tone={sseState === "connected" ? "good" : "warn"}>
            <Radio size={16} />
            SSE {sseState === "connected" ? "实时" : "重连中"}
          </StatusPill>
        </div>
      </header>

      <section className="layout">
        <aside className="sidebar">
          <div className="section-title">设备</div>
          <div className="device-list">
            {devices.length === 0 ? (
              <div className="empty-state">暂无设备</div>
            ) : (
              devices.map((device) => (
                <button
                  key={device.deviceId}
                  className={`device-button ${device.deviceId === selectedDevice?.deviceId ? "active" : ""}`}
                  onClick={() => setSelectedDeviceId(device.deviceId)}
                >
                  <span className={`presence-dot ${device.online ? "online" : ""}`} />
                  <span>{device.deviceId}</span>
                </button>
              ))
            )}
          </div>

          <div className="section-title">最近事件</div>
          <div className="event-list">
            {eventLog.length === 0 ? (
              <div className="empty-state">等待事件</div>
            ) : (
              eventLog.slice(0, MAX_EVENT_LOG_ITEMS).map((item) => (
                <div className="event-item" key={item.id}>
                  <span>{item.messageType}</span>
                  <time>{formatTime(item.receivedAt)}</time>
                </div>
              ))
            )}
          </div>
        </aside>

        <section className="content">
          <div className="device-header">
            <div>
              <div className="eyebrow">当前设备</div>
              <h2>{selectedDevice?.deviceId ?? "未发现设备"}</h2>
            </div>
            <div className="device-header-actions">
              <StatusPill tone={online ? "good" : "danger"}>
                <Activity size={16} />
                {online ? "在线" : "离线"}
              </StatusPill>
              <StatusPill tone={fault === "none" ? "good" : "danger"}>
                <AlertTriangle size={16} />
                {fault === "none" ? "无故障" : faultLabel(fault)}
              </StatusPill>
            </div>
          </div>

          <div className="stat-grid">
            <StatTile icon={mode === "auto" ? <RotateCcw size={20} /> : <SlidersHorizontal size={20} />} label="模式" value={mode} tone={mode === "auto" ? "good" : "warn"} />
            <StatTile icon={<Lightbulb size={20} />} label="亮度" value={`${currentBrightness}%`} tone={currentBrightness > 0 ? "warn" : "default"} />
            <StatTile icon={dark ? <Moon size={20} /> : <Sun size={20} />} label="光照" value={dark ? "夜晚" : "白天"} tone={dark ? "info" : "good"} />
            <StatTile icon={<Users size={20} />} label="人流" value={occupied ? "有人" : "无人"} tone={occupied ? "warn" : "good"} />
            <StatTile icon={<Gauge size={20} />} label="电流" value={`${asNumber(telemetry.currentMa).toFixed(1)} mA`} />
            <StatTile icon={<Zap size={20} />} label="总线电压" value={`${asNumber(telemetry.busVoltageV).toFixed(3)} V`} />
            <StatTile icon={<Radio size={20} />} label="YOLO" value={yoloStatus.label} tone={yoloStatus.statTone} />
            <StatTile icon={<Clock3 size={20} />} label="最后更新" value={formatDateTime(selectedDevice?.lastSeenAt ?? null)} />
          </div>

          <div className="workspace-grid">
            <section className="panel control-panel">
              <div className="panel-heading">
                <div>
                  <div className="eyebrow">Control</div>
                  <h3>远程控制</h3>
                </div>
                {busyAction && <Loader2 className="spin" size={20} />}
              </div>

              <div className="quick-actions">
                <button className="icon-button" onClick={() => runCommand("auto", { mode: "auto" })} disabled={!selectedDevice || !!busyAction}>
                  <RotateCcw size={18} />
                  自动
                </button>
                <button className="icon-button" onClick={() => runCommand("off", manualPayload(0))} disabled={!selectedDevice || !!busyAction}>
                  <Power size={18} />
                  关灯
                </button>
                <button className="icon-button" onClick={() => runCommand("dim", manualPayload(30))} disabled={!selectedDevice || !!busyAction}>
                  <Lightbulb size={18} />
                  30%
                </button>
                <button className="icon-button" onClick={() => runCommand("full", manualPayload(100))} disabled={!selectedDevice || !!busyAction}>
                  <Lightbulb size={18} />
                  100%
                </button>
              </div>

              <label className="field-label" htmlFor="brightness">
                手动亮度
                <span>{brightness}%</span>
              </label>
              <input
                id="brightness"
                className="brightness-slider"
                type="range"
                min="0"
                max="100"
                value={brightness}
                onChange={(event) => setBrightness(Number(event.target.value))}
              />

              <div className="control-row">
                <label className="field-label compact" htmlFor="timeout">
                  手动时长
                </label>
                <select id="timeout" value={timeoutMode} onChange={(event) => setTimeoutMode(event.target.value)}>
                  <option value="default">默认 120 秒</option>
                  <option value="30000">30 秒</option>
                  <option value="120000">120 秒</option>
                  <option value="0">长期</option>
                </select>
              </div>

              <div className="control-row actions">
                <button className="primary-button" onClick={() => runCommand("manual", manualPayload(brightness))} disabled={!selectedDevice || !!busyAction}>
                  <Send size={18} />
                  下发亮度
                </button>
                <button className="ghost-button" onClick={runBroadcastAuto} disabled={!!busyAction}>
                  <Radio size={18} />
                  广播自动
                </button>
              </div>

              {(feedback || error) && <div className={`feedback ${error ? "error" : ""}`}>{error || feedback}</div>}
            </section>

            <section className="panel detail-panel">
              <div className="panel-heading">
                <div>
                  <div className="eyebrow">Telemetry</div>
                  <h3>实时状态</h3>
                </div>
                <CheckCircle2 size={20} />
              </div>
              <div className="detail-grid">
                <span>D23</span>
                <strong>{String(telemetry.d23Level ?? "-")}</strong>
                <span>streetlightOn</span>
                <strong>{String(telemetry.streetlightOn ?? "-")}</strong>
                <span>manualTimeoutMs</span>
                <strong>{String(telemetry.manualTimeoutMs ?? "-")}</strong>
                <span>peopleCount</span>
                <strong>{String(telemetry.peopleCount ?? "-")}</strong>
                <span>commandAck</span>
                <strong>{asString(commandAck.message, "-")}</strong>
                <span>commandAckAt</span>
                <strong>{formatTime(selectedDevice?.commandAckAt ?? null)}</strong>
              </div>
            </section>

            <section className="panel camera-panel">
              <div className="panel-heading">
                <div>
                  <div className="eyebrow">Camera</div>
                  <h3>摄像头与 YOLO</h3>
                </div>
                <StatusPill tone={yoloStatus.pillTone}>
                  <Camera size={16} />
                  {yoloStatus.label}
                </StatusPill>
              </div>

              <div className="camera-content">
                <div className="camera-actions">
                  <button className="primary-button" onClick={runVisionStart} disabled={!selectedDevice || !!busyAction || !!visionServiceStatus?.running}>
                    <Play size={18} />
                    启动检测
                  </button>
                  <button className="ghost-button" onClick={runVisionStop} disabled={!!busyAction || !visionServiceStatus?.running}>
                    <Square size={18} />
                    停止检测
                  </button>
                  <button className="icon-button" onClick={() => openCameraPath("/capture")}>
                    <Camera size={18} />
                    打开快照
                  </button>
                  <button className="icon-button" onClick={() => openCameraPath("/stream")}>
                    <Video size={18} />
                    打开实时流
                  </button>
                  <button className="icon-button" onClick={() => openCameraPath("/")}>
                    <ExternalLink size={18} />
                    摄像头首页
                  </button>
                </div>

                <div className="yolo-summary">
                  <div className="vision-process-card">
                    <div className="vision-process-line">
                      <StatusPill tone={visionProcessTone}>
                        {visionServiceStatus?.running ? <Loader2 className="spin" size={16} /> : <Radio size={16} />}
                        {getVisionProcessLabel(visionServiceStatus)}
                      </StatusPill>
                      <span>{visionServiceStatus?.message ?? "等待后端状态"}</span>
                    </div>
                    {visionServiceStatus?.source && <div className="vision-process-source">检测源 {visionServiceStatus.source}</div>}
                    {latestVisionLog && <div className="vision-log-line">最近日志 {latestVisionLog}</div>}
                    {(visionFeedback || visionError) && <div className={`feedback compact-feedback ${visionError ? "error" : ""}`}>{visionError || visionFeedback}</div>}
                  </div>
                  <div className="yolo-status-line">
                    <StatusPill tone={yoloStatus.pillTone}>
                      <Radio size={16} />
                      {yoloStatus.description}
                    </StatusPill>
                    <span>视频源 {cameraBase}</span>
                  </div>
                  <div className="camera-detail-grid">
                    <span>peopleCount</span>
                    <strong>{String(peopleCount)}</strong>
                    <span>occupied</span>
                    <strong>{occupied ? "有人" : "无人"}</strong>
                    <span>visionAt</span>
                    <strong>{formatTime(selectedDevice?.visionAt ?? null)}</strong>
                    <span>confidence</span>
                    <strong>{visionConfidence.toFixed(3)}</strong>
                  </div>
                  <p className="camera-note">实时流会占用 ESP32-S3-CAM /stream；运行 YOLO 时建议只打开快照。</p>
                </div>
              </div>
            </section>
          </div>

          <section className="panel fault-panel">
            <div className="panel-heading">
              <div>
                <div className="eyebrow">Faults</div>
                <h3>报修记录</h3>
              </div>
              <button className="small-icon-button" onClick={() => getFaults().then(setFaults).catch(() => undefined)} aria-label="刷新故障记录">
                <RefreshCw size={18} />
              </button>
            </div>
            <div className="repair-list">
              {faults.length === 0 ? (
                <div className="fault-empty">暂无故障事件</div>
              ) : (
                faults.slice(0, 6).map((item) => (
                  <div className={`repair-item ${item.repairRequired ? "repair-required" : "repair-cleared"}`} key={`${item.deviceId}-${item.receivedAt}-${item.fault}`}>
                    <div className="repair-status-icon">
                      {item.repairRequired ? <AlertTriangle size={18} /> : <CheckCircle2 size={18} />}
                    </div>
                    <div className="repair-main">
                      <div className="repair-title-row">
                        <strong>{faultLabel(item.fault)}</strong>
                        <StatusPill tone={item.repairRequired ? "danger" : "good"}>
                          {item.repairRequired ? "需要报修" : "已恢复"}
                        </StatusPill>
                      </div>
                      <p>{faultMessage(item.message)}</p>
                      <div className="repair-meta">
                        <span>设备 {item.deviceId}</span>
                        <span>位置 {item.location || "-"}</span>
                        <span>电流 {asNumber(item.payload.currentMa).toFixed(1)} mA</span>
                        <span>{formatDateTime(item.receivedAt)}</span>
                      </div>
                    </div>
                  </div>
                ))
              )}
            </div>
          </section>
        </section>
      </section>
    </main>
  );
}

export default App;
