# 基于 ESP32-S3 光照与人流感知的智能路灯管控系统

本项目是一个面向课程设计和全国大学生物联网设计竞赛的智能路灯原型系统。每个路灯节点使用一块 ESP32-S3-CAM + OV3660，接入光照检测、电流检测、PWM 路灯控制、摄像头采集和 MQTT 通信，并通过电脑端 YOLOv8 视觉服务实现夜间人流感知。当前已完成 `GLG-A-001`、`GLG-A-002` 两个节点的联网联调和批量控制验证。

系统当前已实现：

- 根据 D23 光敏模块判断白天/夜晚。
- 根据 YOLOv8 行人检测结果判断是否有人。
- 夜晚无人时降低路灯亮度，夜晚有人时恢复高亮。
- 支持 EMQX/MQTT 平台远程手动开关灯。
- 支持平台通过广播命令主题远程批量管控多盏路灯。
- 通过 INA219 检测 LED 电流并判断断路、异常电流、过流等故障。
- 故障状态变化时通过独立 fault 主题发布报修信息和位置。
- ESP32-S3-CAM 提供 HTTP 拍照和视频流。
- 电脑端 Python 服务读取 ESP32-S3-CAM 视频流，发布视觉识别结果到 EMQX。
- vision 超时安全回退：AI 服务中断时，夜晚自动回到高亮，避免误判无人导致过暗。
- 已加入 Spring Boot 后端和 React Web/PWA 前端，可实时展示设备状态、摄像头入口、YOLO 服务状态，并通过 REST API 下发单灯/批量控制命令。
- 后端可按设备启动独立 YOLO 进程，两台设备能够同时进行摄像头实时检测。
- 提供公网 Web/PWA 演示入口，访客可查看状态，管理员通道可进行受限控制。

## 系统架构

```text
D23 光敏模块  ----\
INA219 电流检测 ----> ESP32-S3-CAM 节点 ----> EMQX 主题 telemetry / fault / commandAck
LED 路灯 PWM   ----/       ^
                          |
OV3660 摄像头 -> HTTP /stream -> 对应设备的 YOLOv8 进程 -> EMQX vision

Web/PWA 前端 -> Spring Boot 后端 -> EMQX 单灯/广播 command -> ESP32-S3-CAM 节点
EMQX 主题 telemetry / vision / fault / commandAck -> Spring Boot 后端 -> Web/PWA 前端
```

YOLOv8 不直接运行在 ESP32-S3-CAM 上。ESP32-S3-CAM 负责采集图像并提供 HTTP 视频流，电脑或后端 Python 服务负责 AI 识别，再把识别结果通过 MQTT 发给设备。

## 工程目录

```text
smart_streetlight
├── main/                    ESP-IDF 固件源码
│   ├── main.c               主循环，整合传感器、MQTT、策略和安全回退
│   ├── hardware_config.h    引脚、INA219、摄像头和故障阈值配置
│   ├── policy_config.h      路灯策略参数
│   ├── camera_service.c     ESP32-S3-CAM HTTP /capture 和 /stream
│   ├── mqtt_service.c       EMQX MQTT 连接、订阅和发布
│   ├── streetlight_policy.c 自动/手动亮度策略
│   ├── streetlight_control.c LEDC PWM 输出
│   ├── d23_sensor.c         D23 光敏模块读取
│   ├── ina219_sensor.c      INA219 电压/电流检测
│   ├── fault_detector.c     路灯故障判断
│   ├── command_parser.c     平台 command JSON 解析
│   └── vision_parser.c      YOLO vision JSON 解析
├── vision/                  电脑端 YOLOv8 视觉服务
│   ├── vision_service.py    实时读取视频流并发布 MQTT vision
│   ├── vision_detect.py     图片/视频/批量检测工具
│   ├── people_counter.py    行人检测结果汇总
│   ├── tests/               Python 单元测试
│   └── README.md            YOLO 子模块详细说明
├── backend/                 Spring Boot 后端服务
│   ├── pom.xml              Maven 工程配置
│   ├── src/main/java/       后端源码
│   └── src/main/resources/  后端配置
├── frontend/                Web/PWA 管理端
│   ├── src/                 React 前端源码
│   ├── public/              PWA manifest 和图标
│   └── package.json         前端依赖和脚本
├── components/              esp32-camera 组件
├── sdkconfig                当前 ESP-IDF 配置
└── yolov8n.pt               YOLOv8n 模型文件
```

## 硬件连接

当前已验证的引脚分配如下：

| 模块 | 连接 |
| --- | --- |
| D23 VCC | 3V3 |
| D23 GND | GND |
| D23 SIG | GPIO1 / IO1 |
| INA219 SDA | GPIO41 / IO41 |
| INA219 SCL | GPIO42 / IO42 |
| LED/PWM 控制 | GPIO14 / IO14 |
| LED 电流检测串联路径 | GPIO14 -> INA219 VIN+ -> INA219 VIN- -> LED -> GND |

GOOUUU ESP32-S3-CAM 的 OV3660 摄像头占用以下固定引脚，不要再拿这些 GPIO 接外设：

```text
XCLK  GPIO15
SIOD  GPIO4
SIOC  GPIO5
D0    GPIO11
D1    GPIO9
D2    GPIO8
D3    GPIO10
D4    GPIO12
D5    GPIO18
D6    GPIO17
D7    GPIO16
VSYNC GPIO6
HREF  GPIO7
PCLK  GPIO13
```

注意：

- 当前 D23 模块经电位器调好后，黑暗时输出 `0`，光照时输出 `1`。
- `GPIO14` 输出 PWM 控制 LED 亮度，同时通过 INA219 串联检测 LED 电流。
- 不建议把 LED 控制线放在 ESP32-S3 启动敏感引脚上。

## 控制策略

自动模式下：

| 场景 | 期望亮度 |
| --- | --- |
| 白天 / D23 光照 | 0% |
| 夜晚 + YOLO 检测有人 | 100% |
| 夜晚 + YOLO 检测无人 | 30% |
| 夜晚 + vision 超过 15 秒无更新 | 100% 安全回退 |

手动模式下，平台命令优先。旧的开/关命令继续有效：不写 `brightnessPercent` 时，`streetlightOn:true` 等价于 100%，`streetlightOn:false` 等价于 0%。

```json
{"mode":"manual","streetlightOn":true}
```

```json
{"mode":"manual","streetlightOn":false}
```

也可以直接指定手动亮度，范围为 0-100：

```json
{"mode":"manual","brightnessPercent":59}
```

指定亮度后，固件会按亮度自动推导 `streetlightOn`：`brightnessPercent` 大于 0 时为开灯，等于 0 时为关灯。若同时包含 `streetlightOn` 和 `brightnessPercent`，两者必须一致，例如：

```json
{"mode":"manual","streetlightOn":true,"brightnessPercent":59}
```

下面这种互相矛盾的命令会被拒绝：

```json
{"mode":"manual","streetlightOn":false,"brightnessPercent":59}
```

默认手动覆盖会在 120 秒后自动回到 `auto`，避免演示或运维时忘记恢复自动策略。若确实需要长期保持手动模式，可显式关闭超时：

```json
{"mode":"manual","streetlightOn":true,"manualTimeoutMs":0}
```

也可以指定本次手动覆盖持续时间，例如 30 秒：

```json
{"mode":"manual","streetlightOn":true,"manualTimeoutMs":30000}
```

指定亮度时也可以同时指定本次手动覆盖持续时间：

```json
{"mode":"manual","brightnessPercent":59,"manualTimeoutMs":30000}
```

恢复自动模式：

```json
{"mode":"auto"}
```

## MQTT 主题

默认设备 ID：

```text
GLG-A-001
```

| 主题 | 方向 | 说明 |
| --- | --- | --- |
| `streetlight/GLG-A-001/telemetry` | ESP32 -> EMQX | 周期状态上报 |
| `streetlight/GLG-A-001/command` | EMQX -> ESP32 | 平台下行控制命令 |
| `streetlight/all/command` | EMQX -> 所有 ESP32 | 平台批量控制广播命令 |
| `streetlight/GLG-A-001/commandAck` | ESP32 -> EMQX | 命令处理回执 |
| `streetlight/GLG-A-001/vision` | YOLO 服务 -> EMQX -> ESP32 | 行人检测结果 |
| `streetlight/GLG-A-001/fault` | ESP32 -> EMQX | 故障报修和恢复事件 |
| `streetlight/GLG-A-001/deviceInfo` | ESP32 -> EMQX | 设备联网信息和摄像头访问地址 |

单灯控制和批量控制使用相同 payload。区别只在发布主题：

```text
单灯控制：streetlight/GLG-A-001/command
批量控制：streetlight/all/command
```

批量命令执行后，每个设备仍然通过自己的 `streetlight/<deviceId>/commandAck` 回执，便于平台确认哪些设备已执行。

## 后端服务

后端位于：

```text
backend/
```

推荐用 IntelliJ IDEA 只打开 `D:\esp\smart_streetlight\backend`，作为独立 Maven / Spring Boot 项目开发；固件仍然用 VS Code + ESP-IDF 打开 `D:\esp\smart_streetlight`。

后端当前职责：

- 连接 EMQX。
- 订阅：

```text
streetlight/+/telemetry
streetlight/+/vision
streetlight/+/fault
streetlight/+/commandAck
streetlight/+/deviceInfo
```

- 在内存中缓存每台设备的最新 telemetry、vision、fault、commandAck、deviceInfo。
- 给前端提供 REST API。
- 通过 EMQX 下发单灯命令和批量广播命令。
- 按设备托管电脑端 YOLO 视觉服务的启动、停止和状态查询，支持多设备同时检测。

首次运行前设置环境变量，不要把真实密码写进代码：

```powershell
$env:MQTT_HOST="your-emqx-host"
$env:MQTT_PORT="8883"
$env:MQTT_TLS="true"
$env:MQTT_USERNAME="your-username"
$env:MQTT_PASSWORD="your-password"
```

如果要从 Web 前端启动 YOLO 服务，建议先在 Anaconda PowerShell Prompt 中查看 `yolov8-env` 的 Python 路径：

```powershell
conda activate yolov8-env
python -c "import sys; print(sys.executable)"
```

然后在运行后端的 PowerShell 中增加：

```powershell
$env:VISION_PYTHON_COMMAND="上一步输出的python.exe完整路径"
$env:VISION_WORKING_DIRECTORY="D:\esp\smart_streetlight"
```

也可以使用 conda 启动：

```powershell
$env:VISION_CONDA_ENV="yolov8-env"
$env:VISION_WORKING_DIRECTORY="D:\esp\smart_streetlight"
```

如果普通 PowerShell 找不到 `conda`，再额外设置 `VISION_CONDA_COMMAND` 为 `conda.exe` 完整路径。

运行：

```powershell
cd D:\esp\smart_streetlight\backend
.\run-backend.ps1
```

如果当前终端没有 `mvn` 命令，`run-backend.ps1` 会自动尝试使用本机 `.m2/wrapper/dists` 目录里已经下载过的 Maven。也可以用 IntelliJ IDEA 打开 `backend` 文件夹，导入 Maven 项目后运行 `SmartStreetlightBackendApplication`。

后端 API：

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

### 公网 Web 访问模式

在线演示：<https://eaea499.cn/esp32/>

个人网站的智能路灯项目说明页也提供了入口：<https://eaea499.cn/projects/smart-streetlight/>

前端提供两个访问通道：

- `/esp32/`：通道选择页。
- `/esp32/guest`：访客只读通道，可查看设备状态、遥测、故障和 SSE 实时事件。
- `/esp32/admin`：管理员通道，登录后开放路灯控制和 YOLO 服务启停。

访客页面只展示设备在线状态、遥测数据、故障记录和视觉识别摘要，不展示摄像头内网地址、YOLO 进程日志或摄像头访问按钮。管理员页面才显示远程控制、摄像头入口和视觉服务管理功能。

后端通过 Spring Security 会话和 CSRF 防护限制写操作。访客即使直接请求控制接口，也不能发布 MQTT 命令。演示登录凭据只用于网站展示，不应与 MQTT 或服务器密码复用；正式部署时应替换为独立账号、强密码和更严格的访问策略。

公网部署结构如下：

```text
浏览器 -> Nginx HTTPS
          ├── /                 个人网站静态文件
          ├── /esp32/           智能路灯 React/PWA 前端
          └── /esp32-api/       Spring Boot API、SSE 和会话认证
                                      |
                                      v
                                    EMQX/MQTT
```

单灯控制：

```http
POST /api/devices/GLG-A-001/command
Content-Type: application/json

{"mode":"manual","brightnessPercent":59,"manualTimeoutMs":30000}
```

批量广播控制：

```http
POST /api/devices/batch-command
Content-Type: application/json

{"broadcast":true,"mode":"auto"}
```

当前后端是 MVP 阶段，状态先保存在内存中，便于演示实时闭环。后续可增加 MySQL 持久化 telemetry、vision、fault、commandAck 和 command 记录，用于历史曲线、故障追踪、能耗统计和课程设计报告中的数据分析。

前端实时刷新建议使用 SSE：

```javascript
const events = new EventSource("http://localhost:8080/api/events/stream");

events.addEventListener("device-state", (event) => {
  const data = JSON.parse(event.data);
  console.log(data.messageType, data.deviceId, data.device);
});
```

后端每收到 `telemetry`、`vision`、`fault`、`commandAck`、`deviceInfo` 消息，就会推送一次 `device-state` 事件。

## Web/PWA 前端

前端位于：

```text
frontend/
```

前端当前职责：

- 展示后端 MQTT 连接状态。
- 展示设备在线状态、光照、亮度、模式、电流、视觉状态和故障状态。
- 通过 SSE 实时接收后端推送的设备状态变化。
- 通过后端 API 下发单设备手动亮度、恢复自动、批量广播自动等控制命令。
- 管理员通道提供 ESP32-S3-CAM 快照、实时流和摄像头首页入口；访客通道仅展示脱敏后的视觉识别摘要。
- 根据 telemetry 中的 `dark`、`visionValid`、`peopleCount` 等字段提示 YOLO 当前状态。
- 通过后端 API 按设备启动/停止电脑端 YOLO 视觉服务，并显示当前设备的后端进程状态。

运行前先确保后端已启动，并且：

```text
http://localhost:8080/api/mqtt/status
```

返回：

```json
{"connected":true}
```

启动前端：

```powershell
cd D:\esp\smart_streetlight\frontend
npm install
npm run dev
```

浏览器打开：

```text
http://localhost:5173
```

手机与开发电脑连接同一 WiFi 后，可使用电脑的局域网 IPv4 地址访问，例如：

```text
http://192.168.117.21:5173/
```

前端未显式配置 `VITE_API_BASE_URL` 时，会自动使用当前页面主机名并连接其 `8080` 端口。因此手机打开上述地址后，会请求 `http://192.168.117.21:8080`，不再错误访问手机自身的 `localhost`。首次访问失败时，应确认 Vite 已允许局域网监听，并允许 Node.js、Java 通过 Windows 防火墙的专用网络。

本项目同时支持两种运行方式：本地局域网联调，以及通过 Nginx 反向代理部署到个人网站的公网演示。公网只暴露 HTTPS 网站、同源 API 和 SSE；ESP32 摄像头视频流仍保持在局域网地址，不直接暴露到互联网。这样既能展示完整的状态闭环，也能避免开发服务器、摄像头入口及控制接口未经认证直接暴露。

如果后端地址不是 `http://localhost:8080`，可在 `frontend/.env.local` 中配置：

```text
VITE_API_BASE_URL=http://localhost:8080
VITE_CAMERA_BASE_URL=http://ESP32_CAM_IP
```

新固件会在每次 MQTT 连接成功后向 `streetlight/<deviceId>/deviceInfo` 上报一次 `cameraBaseUrl`，并设置 retained。前端收到后会优先使用设备上报的摄像头地址，因此 ESP32 重新联网导致 IP 改变时，下一次 `deviceInfo` 到达后前端入口会自动跟随新地址。

`VITE_CAMERA_BASE_URL` 只是兜底地址，例如 `http://192.168.117.237`，不要带 `/stream`。当前还没烧录带 `deviceInfo` 的固件、或设备还没上报摄像头地址时，前端会使用这个兜底值。前端的摄像头入口不会默认加载实时流，只有点击按钮时才会打开对应页面：

```text
快照：http://ESP32_CAM_IP/capture
实时流：http://ESP32_CAM_IP/stream
首页：http://ESP32_CAM_IP/
```

YOLO 服务运行时不建议同时长时间打开 `/stream`。ESP32-S3-CAM 可以处理简单 HTTP 访问，但多路 MJPEG 实时流会明显增加压力，可能导致 OpenCV 读取超时。需要观察画面时，优先打开 `/capture` 快照；需要调试摄像头时，再临时打开 `/stream`。

telemetry 示例：

```json
{
  "deviceId": "GLG-A-001",
  "uptimeMs": 123456,
  "d23Level": 0,
  "dark": true,
  "streetlightOn": true,
  "brightnessPercent": 30,
  "mode": "auto",
  "manualTimeoutMs": 0,
  "ina219Ok": true,
  "busVoltageV": 0.556,
  "shuntVoltageMv": 0.99,
  "currentMa": 9.9,
  "visionValid": true,
  "occupied": false,
  "peopleCount": 0,
  "visionConfidence": 0,
  "fault": "none"
}
```

vision 示例：

```json
{
  "deviceId": "GLG-A-001",
  "source": "http://ESP32_CAM_IP/stream",
  "timestampMs": 123456789,
  "peopleCount": 1,
  "occupied": true,
  "maxConfidence": 0.483,
  "frameIndex": 226
}
```

deviceInfo 示例：

```json
{
  "deviceId": "GLG-A-001",
  "uptimeMs": 123456,
  "cameraBaseUrl": "http://192.168.117.237"
}
```

`deviceInfo` 只在设备 MQTT 连接成功后发布，并设置 retained，用于让后端和前端记住最新摄像头入口；周期 telemetry 不携带该地址。

## 固件配置与烧录

推荐在 ESP-IDF PowerShell 环境中操作：

```powershell
. 'C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1'
cd D:\esp\smart_streetlight
idf.py set-target esp32s3
idf.py menuconfig
```

在 `menuconfig` 中检查或修改：

```text
Smart Streetlight Configuration
├── WiFi SSID
├── WiFi Password
├── Device ID
├── EMQX MQTT URI
├── EMQX MQTT Username
├── EMQX MQTT Password
└── Streetlight Location
```

不要把真实 EMQX 密码提交到公开仓库或报告附件中。

构建：

```powershell
idf.py build
```

烧录并打开串口监视器：

```powershell
idf.py -p COMx flash monitor
```

其中 `COMx` 替换为实际串口号。烧录后在 monitor 中查看 WiFi 获取到的 IP，例如：

```text
got ip: 192.168.0.237
```

摄像头服务地址：

```text
http://ESP32_CAM_IP/
http://ESP32_CAM_IP/capture
http://ESP32_CAM_IP/stream
```

## 运行 YOLOv8 视觉服务

在 Anaconda PowerShell Prompt 中运行：

```powershell
conda activate yolov8-env
cd D:\esp\smart_streetlight
```

检查依赖：

```powershell
python -c "import ultralytics; print(ultralytics.__version__)"
python -c "import cv2; print(cv2.__version__)"
python -c "import paho.mqtt.client; print('paho-mqtt ok')"
```

启动实时视觉服务：

```powershell
python -m vision.vision_service `
  --source http://ESP32_CAM_IP/stream `
  --conf 0.35 `
  --frame-stride 15 `
  --publish-interval-ms 2000 `
  --stable-window 5 `
  --occupied-min-hits 2 `
  --empty-min-hits 4 `
  --auto-pause-by-dark `
  --host your-emqx-host `
  --port 8883 `
  --tls `
  --username your-username `
  --password your-password
```

说明：

- `--source` 填 ESP32-S3-CAM 的 `/stream` 地址。
- `--conf 0.35` 是当前较稳的置信度阈值，可按实际画面微调。
- `--frame-stride 15` 表示每 15 帧做一次 YOLO 检测。
- `--publish-interval-ms 2000` 表示同状态约 2 秒发布一次心跳。
- 稳定窗口逻辑为 5 次检测中至少 2 次有人才判定有人，至少 4 次无人再判定无人。
- `--auto-pause-by-dark` 表示订阅 telemetry，并在 `dark:false` 时暂停读取视频流和 YOLO 推理，`dark:true` 时自动恢复。
- 正常演示时不要加 `--include-debug-fields`，避免 vision 消息过长。

更多图片、视频和批量检测用法见 [vision/README.md](vision/README.md)。

## 本地视频检测与保存

`vision.vision_service` 主要用于实时服务和 MQTT 发布，不负责把标注后的视频保存为结果文件。若要把本地视频用 YOLOv8 处理后保存到 `vision\output`，使用 `vision.vision_detect`：

```powershell
conda activate yolov8-env
cd D:\esp\smart_streetlight
python -m vision.vision_detect --source vision\data\video9.mp4 --output vision\output\video9_result.mp4 --model yolov8n.pt
```

如果觉得处理太慢，可以先用跳帧检测生成演示结果：

```powershell
python -m vision.vision_detect --source vision\data\video9.mp4 --output vision\output\video9_result_fast.mp4 --model yolov8n.pt --frame-stride 5
```

如果只是快速确认能跑通，可以只处理前 300 帧：

```powershell
python -m vision.vision_detect --source vision\data\video9.mp4 --output vision\output\video9_result_preview.mp4 --model yolov8n.pt --frame-stride 5 --max-frames 300
```

说明：

- `--frame-stride 1` 表示每一帧都做 YOLO 检测，效果最完整但最慢。
- 默认 `--frame-stride 5` 表示每 5 帧检测一次，其余帧原样写入，速度更适合课程展示素材。
- `--max-frames` 只适合预览，不适合最终完整视频。
- 终端持续输出 JSON 是正常现象，表示每次检测帧的结构化识别结果。

## 推荐验证流程

1. 烧录固件，确认串口输出 WiFi 已连接、MQTT 已连接。
2. 浏览器打开 `http://ESP32_CAM_IP/stream`，确认摄像头画面正常。
3. MQTTX 订阅：

```text
streetlight/GLG-A-001/telemetry
streetlight/GLG-A-001/commandAck
streetlight/GLG-A-001/vision
streetlight/GLG-A-001/fault
streetlight/GLG-A-001/deviceInfo
```

4. 发布手动开灯命令：

```json
{"mode":"manual","streetlightOn":true}
```

应看到灯亮，并收到 `commandAck`。

5. 发布手动调光命令：

```json
{"mode":"manual","brightnessPercent":59}
```

应看到灯以约 59% PWM 亮度点亮，并收到 `commandAck`，其中 `brightnessPercent` 应为 `59`。

6. 发布手动关灯命令：

```json
{"mode":"manual","streetlightOn":false}
```

应看到灯灭，并收到 `commandAck`。

7. 测试批量控制广播主题。向 `streetlight/all/command` 发布：

```json
{"mode":"manual","brightnessPercent":59,"manualTimeoutMs":30000}
```

当前设备也应执行命令并通过 `streetlight/GLG-A-001/commandAck` 回执。多设备部署时，每台设备都会收到该广播命令，并分别通过自己的 `commandAck` 回执。

8. 发布自动模式命令：

```json
{"mode":"auto"}
```

后续验证光照和 YOLO 自动策略前，务必确认 telemetry 中有：

```text
mode:"auto"
```

如果忘记下发 `{"mode":"auto"}`，默认手动覆盖也会在 120 秒后自动回到 `auto`。

9. D23 光照，应看到：

```text
dark:false
brightnessPercent:0
```

10. 遮住 D23 并运行 YOLO 服务，无人时应看到：

```text
dark:true
visionValid:true
occupied:false
brightnessPercent:30
```

11. 遮住 D23 并让摄像头检测到人，应看到：

```text
dark:true
visionValid:true
occupied:true
brightnessPercent:100
```

12. 遮住 D23，先让无人低亮生效，再停止 YOLO 服务。约 15 秒后应看到：

```text
visionValid:false
occupied:false
peopleCount:0
visionConfidence:0
brightnessPercent:100
```

这说明 vision 超时安全回退成功。

## 故障检测

当前固件通过 INA219 电流数据判断故障，并在 telemetry 的 `fault` 字段中上报当前状态：

| fault | 含义 |
| --- | --- |
| `none` | 无故障 |
| `ina219_offline` | INA219 通信异常 |
| `open_load` | 路灯应亮但电流过低，疑似断路或 LED 损坏 |
| `unexpected_current` | 路灯应灭但仍检测到电流 |
| `over_current` | 电流过大 |

同时，故障状态发生变化时，固件会向独立故障主题发布事件：

```text
streetlight/GLG-A-001/fault
```

故障事件示例：

```json
{
  "deviceId": "GLG-A-001",
  "uptimeMs": 123456,
  "fault": "open_load",
  "repairRequired": true,
  "message": "streetlight may be disconnected or LED failed",
  "location": "NCHU Test Point A",
  "mode": "auto",
  "streetlightOn": true,
  "brightnessPercent": 100,
  "ina219Ok": true,
  "busVoltageV": 0.012,
  "shuntVoltageMv": 0.01,
  "currentMa": 0.1
}
```

故障恢复时也会发布一次事件，`fault` 为 `none`，`repairRequired` 为 `false`。

故障报修测试常用 `{"mode":"manual","streetlightOn":true}` 强制点亮 LED。该手动覆盖默认 120 秒后自动回到 `auto`。测试结束后，如果要立刻继续验证 D23 或 YOLO 自动控制，可以手动下发：

```json
{"mode":"auto"}
```

在超时或切回 `auto` 之前，manual 模式会覆盖自动策略，LED 会保持平台手动下发的状态。

## 常见问题

**1. telemetry 一直显示有人，但 YOLO 终端已经无人。**

检查 YOLO 服务是否加了 `--include-debug-fields`。正常演示不要加该参数，避免 vision 消息过长。当前固件已把 vision 解析上限放宽到 512 字节，但短消息仍然更稳。

**2. YOLO 服务停止后灯为什么变成 100%？**

这是安全回退。夜晚超过 15 秒没有 vision 更新时，系统认为 AI 服务不可用，自动回到高亮。

**3. 摄像头晚上是否需要一直运行？**

ESP32-S3-CAM HTTP 服务保持运行。电脑端 YOLO 服务加上 `--auto-pause-by-dark` 后，白天会释放 `/stream` 并暂停推理，夜晚自动重新连接并恢复检测。

**4. 光照 D23 后 LED 仍然一直亮怎么办？**

先看 telemetry 的 `mode`。如果是 `manual`，说明平台手动模式仍在覆盖自动策略。可以等待默认 120 秒超时自动回到 `auto`，也可以立即向 `streetlight/GLG-A-001/command` 下发：

```json
{"mode":"auto"}
```

切回 auto 后，再观察 `dark`、`brightnessPercent` 和 `streetlightOn`。

**5. EMQX/MQTTX 的 Host 怎么填？**

Host 只填域名本身，不要把 `Host:` 文本也填进去。端口 TLS 通常使用 `8883`。

**6. 为什么不用 ESP32-S3-CAM 直接跑 YOLO？**

ESP32-S3-CAM 更适合做采集、控制和通信。YOLOv8 推理放在电脑或后端更现实，实时性和识别效果更稳定。

## 当前状态

已验证：

- ESP32-S3-CAM 启动、WiFi、MQTT 正常。
- D23 光照/遮挡判断正常。
- LED PWM 亮度控制正常。
- INA219 电流检测正常。
- EMQX 下行手动控制正常。
- `streetlight/all/command` 批量广播控制正常。
- `GLG-A-001`、`GLG-A-002` 可同时在线，能够分别展示状态和接收单灯命令。
- commandAck 正常。
- fault 独立主题可发布故障报修和恢复事件。
- OV3660 HTTP `/capture` 和 `/stream` 正常。
- YOLOv8 实时检测可通过 MQTT 发布 vision。
- 自动策略：白天关灯、夜晚无人 30%、夜晚有人 100%。
- vision 超时安全回退成功。
- Spring Boot 后端可连接 EMQX、缓存设备状态、提供 REST API/SSE，并可按设备托管启动/停止多个 YOLO 服务。
- React Web/PWA 前端可实时展示状态、控制亮度、查看故障；管理员通道支持摄像头入口和按设备启动/停止 YOLO 服务。
- 访客通道已完成公网展示脱敏，管理员通道通过会话认证保护写操作。
- 两台设备可分别运行独立 YOLO 检测进程，识别结果发布到各自的 `vision` 主题。
- 浏览器可通过个人网站公网地址访问前端，也可在开发阶段通过局域网地址联调。

下一步建议：

- 同步更新竞赛作品创意表和设计文档，明确双节点、多路 YOLO、批量控制、公网 Web 展示和访客/管理员通道已经完成。
- 制作竞赛作品视频，完整展示硬件、自动调光、实时行人检测、手动调光、故障报修、双设备批量控制、安全回退和手机控制。
- 接入 MySQL 保存 telemetry、vision、fault、commandAck 和 command 历史数据，支持历史曲线、故障追踪、能耗统计和报表导出。
- 在历史数据基础上扩展云端大模型或数据分析服务，用于夜间人流趋势分析、亮度策略优化、异常耗电识别和物业报修建议生成。

## 竞赛演示注意事项

- 作品创意表、设计文档、视频字幕和现场讲解应使用相同的功能名称与完成状态。
- 不将原生 APP、MySQL 历史数据、GPS 实时定位或物业工单联动描述为已经实现的功能；公网部署目前主要用于作品展示和联调。
- 视频中的每项功能同时展示操作、实体 LED 响应和前端/EMQX 数据变化，形成可验证闭环。
- 展示 YOLOv8 技术时，可在启动后端前设置 `$env:VISION_SHOW_WINDOW="true"`，使前端启动检测后出现带行人框的 OpenCV 窗口。
- YOLO 运行期间不要长时间同时打开浏览器 `/stream`，需要观察摄像头时优先使用 `/capture` 快照。
- 录屏和拍摄时隐藏 EMQX 用户名、密码及包含凭据的环境变量。
