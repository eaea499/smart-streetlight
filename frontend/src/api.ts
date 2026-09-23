export type JsonObject = Record<string, unknown>;

export interface DeviceStateSnapshot {
  deviceId: string;
  online: boolean;
  lastSeenAt: string | null;
  latestTelemetry: JsonObject;
  telemetryAt: string | null;
  latestVision: JsonObject;
  visionAt: string | null;
  latestFault: JsonObject;
  faultAt: string | null;
  latestCommandAck: JsonObject;
  commandAckAt: string | null;
  latestDeviceInfo: JsonObject;
  deviceInfoAt: string | null;
}

export interface FaultEvent {
  deviceId: string;
  receivedAt: string;
  fault: string | null;
  repairRequired: boolean | null;
  message: string | null;
  location: string | null;
  payload: JsonObject;
}

export interface DeviceEvent {
  deviceId: string;
  messageType: "telemetry" | "vision" | "fault" | "commandAck" | "deviceInfo";
  receivedAt: string;
  payload: JsonObject;
  device: DeviceStateSnapshot;
}

export interface CommandPayload {
  mode: "auto" | "manual";
  streetlightOn?: boolean;
  brightnessPercent?: number;
  manualTimeoutMs?: number;
}

export interface CommandPublishResponse {
  topic: string;
  payload: JsonObject;
  publishedAt: string;
}

export interface VisionServiceStatus {
  running: boolean;
  state: "running" | "stopped" | "exited" | "failed" | string;
  deviceId: string | null;
  source: string | null;
  startedAt: string | null;
  stoppedAt: string | null;
  exitCode: number | null;
  message: string;
  recentLogs: string[];
}

export interface AuthSession {
  authenticated: boolean;
  username: string | null;
  role: string | null;
}

function defaultApiBase(): string {
  if (typeof window !== "undefined" && window.location.hostname) {
    return `${window.location.protocol}//${window.location.hostname}:8080`;
  }
  return "http://localhost:8080";
}

export const API_BASE = (import.meta.env.VITE_API_BASE_URL || defaultApiBase()).replace(/\/$/, "");
export const CAMERA_BASE = (import.meta.env.VITE_CAMERA_BASE_URL || "http://192.168.117.237").replace(/\/$/, "");

let csrfToken: string | null = null;

function getCookie(name: string): string | null {
  if (typeof document === "undefined") {
    return null;
  }
  const prefix = `${name}=`;
  const value = document.cookie.split("; ").find((item) => item.startsWith(prefix))?.slice(prefix.length);
  return value ? decodeURIComponent(value) : null;
}

export async function getCsrfToken(): Promise<string> {
  const response = await fetch(`${API_BASE}/api/auth/csrf`, {
    credentials: "same-origin",
  });
  if (!response.ok) {
    throw new Error("无法初始化安全会话");
  }
  const body = (await response.json()) as { token: string };
  csrfToken = body.token;
  return body.token;
}

async function requestJson<T>(path: string, init?: RequestInit): Promise<T> {
  const method = (init?.method || "GET").toUpperCase();
  const headers = new Headers(init?.headers);
  if (method !== "GET" && method !== "HEAD" && method !== "OPTIONS") {
    const token = csrfToken || getCookie("XSRF-TOKEN") || (await getCsrfToken());
    headers.set("X-XSRF-TOKEN", token);
  }
  if (!headers.has("Content-Type") && method !== "GET" && method !== "HEAD") {
    headers.set("Content-Type", "application/json");
  }

  const response = await fetch(`${API_BASE}${path}`, {
    ...init,
    credentials: "same-origin",
    headers,
  });

  if (!response.ok) {
    let message = `${response.status} ${response.statusText}`;
    try {
      const body = await response.json();
      if (body?.message) {
        message = String(body.message);
      }
    } catch {
      // Keep the HTTP status message when the response is not JSON.
    }
    throw new Error(message);
  }

  if (response.status === 204) {
    return undefined as T;
  }
  return response.json() as Promise<T>;
}

export function getAuthSession(): Promise<AuthSession> {
  return requestJson("/api/auth/session");
}

export function login(username: string, password: string): Promise<AuthSession> {
  return requestJson("/api/auth/login", {
    method: "POST",
    body: JSON.stringify({ username, password }),
  });
}

export function logout(): Promise<void> {
  return requestJson("/api/auth/logout", { method: "POST" });
}

export function getMqttStatus(): Promise<{ connected: boolean }> {
  return requestJson("/api/mqtt/status");
}

export function getDevices(): Promise<DeviceStateSnapshot[]> {
  return requestJson("/api/devices");
}

export function getFaults(limit = 20): Promise<FaultEvent[]> {
  return requestJson(`/api/faults?limit=${limit}`);
}

export function publishDeviceCommand(deviceId: string, payload: CommandPayload): Promise<CommandPublishResponse> {
  return requestJson(`/api/devices/${encodeURIComponent(deviceId)}/command`, {
    method: "POST",
    body: JSON.stringify(payload),
  });
}

export function publishBroadcastCommand(payload: CommandPayload): Promise<{ results: CommandPublishResponse[] }> {
  return requestJson("/api/devices/batch-command", {
    method: "POST",
    body: JSON.stringify({ broadcast: true, ...payload }),
  });
}

export function getVisionServiceStatuses(): Promise<VisionServiceStatus[]> {
  return requestJson("/api/vision/status");
}

export function getDeviceVisionServiceStatus(deviceId: string): Promise<VisionServiceStatus> {
  return requestJson(`/api/devices/${encodeURIComponent(deviceId)}/vision/status`);
}

export function startVisionService(deviceId: string): Promise<VisionServiceStatus> {
  return requestJson(`/api/devices/${encodeURIComponent(deviceId)}/vision/start`, {
    method: "POST",
  });
}

export function stopVisionService(deviceId: string): Promise<VisionServiceStatus> {
  return requestJson(`/api/devices/${encodeURIComponent(deviceId)}/vision/stop`, {
    method: "POST",
  });
}

export function stopAllVisionServices(): Promise<VisionServiceStatus[]> {
  return requestJson("/api/vision/stop", {
    method: "POST",
  });
}
