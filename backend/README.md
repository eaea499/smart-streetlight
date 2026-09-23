# 智能路灯后端服务

这是智能路灯项目的 Spring Boot 后端服务，用于连接 EMQX、缓存设备最新状态、向 Web/PWA 前端提供 API，并按设备托管 YOLO 视觉服务的启动与停止。

## 当前功能

- 通过 MQTT/TLS 连接 EMQX。
- 订阅 `telemetry`、`vision`、`fault`、`commandAck`、`deviceInfo` 主题。
- 在内存中保存每台设备的最新状态。
- 提供 REST API 给前端读取设备状态和故障记录。
- 提供 SSE 实时事件流，前端可实时接收设备状态变化。
- 支持下发单灯控制命令和批量广播控制命令。
- 支持由后端按 `deviceId` 托管启动/停止多个 YOLO 视觉服务。
- 根据设备上报的 `deviceInfo.cameraBaseUrl` 自动拼接摄像头 `/stream` 地址。

## 本地运行

先设置环境变量：

```powershell
$env:MQTT_HOST="your-emqx-host"
$env:MQTT_PORT="8883"
$env:MQTT_TLS="true"
$env:MQTT_USERNAME="your-username"
$env:MQTT_PASSWORD="your-password"
$env:APP_AUTH_DEMO_USERNAME="demo"
$env:APP_AUTH_DEMO_PASSWORD="your-demo-password"
$env:SERVER_SERVLET_SESSION_COOKIE_SECURE="true"
$env:SERVER_SERVLET_SESSION_COOKIE_HTTP_ONLY="true"
$env:SERVER_SERVLET_SESSION_COOKIE_SAME_SITE="lax"
```

后端提供两个访问层级：访客可以读取 `GET /api/**` 数据，管理员登录后才能调用控制和视觉服务启停接口。演示密码只用于网站登录，不要与 MQTT 或服务器密码复用。

认证接口：

```text
POST /api/auth/login
POST /api/auth/logout
GET  /api/auth/session
GET  /api/auth/csrf
```

如果要在前端点击按钮启动 YOLO 服务，还需要让后端知道使用哪个 Python 环境。推荐先在 Anaconda PowerShell Prompt 中查看 `yolov8-env` 的 Python 路径：

```powershell
conda activate yolov8-env
python -c "import sys; print(sys.executable)"
```

然后在运行后端的 PowerShell 里设置：

```powershell
$env:VISION_PYTHON_COMMAND="上一步输出的python.exe完整路径"
$env:VISION_WORKING_DIRECTORY="D:\esp\smart_streetlight"
```

也可以让后端通过 conda 启动：

```powershell
$env:VISION_CONDA_ENV="yolov8-env"
$env:VISION_WORKING_DIRECTORY="D:\esp\smart_streetlight"
```

如果普通 PowerShell 找不到 `conda`，再设置：

```powershell
$env:VISION_CONDA_COMMAND="你的conda.exe完整路径"
```

然后运行：

```powershell
.\run-backend.ps1
```

这个脚本会优先使用系统 PATH 中的 `mvn`；如果找不到，会自动尝试使用本机 `.m2/wrapper/dists` 目录里已经下载过的 Maven。

如果仍然找不到 Maven，可以用 IntelliJ IDEA 打开 `backend` 文件夹，导入 Maven 项目后运行 `SmartStreetlightBackendApplication`。

## API

```text
GET  /api/mqtt/status
GET  /api/devices
GET  /api/devices/{deviceId}
POST /api/devices/{deviceId}/command
POST /api/devices/batch-command
GET  /api/faults
GET  /api/events/stream
GET  /api/vision/status
GET  /api/devices/{deviceId}/vision/status
POST /api/devices/{deviceId}/vision/start
POST /api/devices/{deviceId}/vision/stop
POST /api/vision/stop
```

单灯控制：

```http
POST /api/devices/GLG-A-001/command
Content-Type: application/json

{"mode":"manual","brightnessPercent":59,"manualTimeoutMs":30000}
```

兼容旧的开/关格式：

```json
{"mode":"manual","streetlightOn":true}
```

`brightnessPercent` 范围为 0-100。若同时包含 `streetlightOn` 和 `brightnessPercent`，两者必须一致，例如 `streetlightOn:true` 搭配 `brightnessPercent:59` 是有效的，`streetlightOn:false` 搭配 `brightnessPercent:59` 会被拒绝。

批量广播控制：

```http
POST /api/devices/batch-command
Content-Type: application/json

{"broadcast":true,"mode":"auto"}
```

指定设备批量控制：

```http
POST /api/devices/batch-command
Content-Type: application/json

{"deviceIds":["GLG-A-001"],"mode":"manual","brightnessPercent":59}
```

启动某台设备的 YOLO 服务：

```http
POST /api/devices/GLG-A-001/vision/start
```

后端会从该设备最新 `deviceInfo.cameraBaseUrl` 拼接 `/stream`，并把 MQTT 连接配置传给 `vision.vision_service`。每个设备会使用独立进程和独立 MQTT clientId，因此可以同时启动 `GLG-A-001`、`GLG-A-002` 等多个检测服务。

停止某台设备的 YOLO 服务：

```http
POST /api/devices/GLG-A-001/vision/stop
```

停止全部由后端托管的 YOLO 服务：

```http
POST /api/vision/stop
```

查看全部 YOLO 服务状态：

```http
GET /api/vision/status
```

查看某台设备的 YOLO 服务状态：

```http
GET /api/devices/GLG-A-001/vision/status
```

## 数据存储与算法扩展

当前后端是 MVP 阶段，状态只保存在内存中，后端重启后会清空。这种方式适合课程设计演示实时闭环。

后续可接入 MySQL，建议至少设计以下历史表：

```text
device_telemetry_history   周期状态、光照、亮度、电流、电压、模式
device_vision_history      peopleCount、occupied、confidence、source
device_fault_history       故障类型、报修状态、位置、恢复时间
device_command_history     下发命令、目标设备、执行回执、结果
```

在此基础上可以做历史数据管理：

- 查询某个设备一段时间内的亮度、电流和故障变化。
- 统计夜间无人低亮时长，用于估算节能效果。
- 追踪 `open_load` 等故障发生和恢复过程。
- 给前端增加历史曲线、故障列表、报修记录和导出报表。

算法方面，当前项目采用 YOLOv8 人流识别 + 规则型控制策略。后续可把 MySQL 历史数据接入云端大模型或数据分析服务，用于：

- 预测夜间人流高峰，提前优化亮度策略。
- 根据电流和亮度历史识别异常耗电或灯具老化。
- 自动生成物业报修建议和故障摘要。
- 为不同路段生成差异化节能控制参数。

## 实时事件

前端可以用浏览器原生 `EventSource` 订阅：

```javascript
const events = new EventSource("http://localhost:8080/api/events/stream");

events.addEventListener("device-state", (event) => {
  const data = JSON.parse(event.data);
  console.log(data.messageType, data.deviceId, data.device);
});
```

`device-state` 事件会在后端收到以下 MQTT 消息时触发：

```text
telemetry
vision
fault
commandAck
deviceInfo
```

事件数据包含：

```json
{
  "deviceId": "GLG-A-001",
  "messageType": "telemetry",
  "receivedAt": "2026-07-08T10:00:00Z",
  "payload": {},
  "device": {}
}
```
