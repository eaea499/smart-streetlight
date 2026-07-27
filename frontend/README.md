# 智能路灯前端

这是智能路灯项目的 Web/PWA 管理端，基于 Vite + React + TypeScript。

## 当前功能

- 显示后端 MQTT 连接状态。
- 显示设备在线状态、光照、亮度、模式、电流、视觉状态和故障状态。
- 通过 SSE 实时接收后端推送的设备状态变化。
- 支持单设备下发 `auto`、关灯、30%、100% 和自定义亮度。
- 支持广播恢复自动模式。
- 显示最近事件和故障记录，最近事件列表会限制高度，避免页面被持续 telemetry 拉长。
- 提供 ESP32-S3-CAM 快照、实时流和摄像头首页入口。
- 根据 telemetry 显示 YOLO 服务提示：白天暂停、检测中或等待更新。
- 支持通过后端按设备启动/停止 YOLO 视觉服务，并显示当前设备的后端进程状态。

## 本地运行

确保后端已经运行在：

```text
http://localhost:8080
```

安装依赖：

```powershell
cd D:\esp\smart_streetlight\frontend
npm install
```

启动前端：

```powershell
npm run dev
```

浏览器打开：

```text
http://localhost:5173
```

如果后端不是 `localhost:8080`，复制 `.env.example` 为 `.env.local`，修改：

```text
VITE_API_BASE_URL=http://localhost:8080
VITE_CAMERA_BASE_URL=http://ESP32_CAM_IP
```

`VITE_CAMERA_BASE_URL` 是兜底地址。新固件会通过 `streetlight/<deviceId>/deviceInfo` 上报 `cameraBaseUrl`，前端收到后会优先使用设备上报的地址。兜底地址填 ESP32-S3-CAM 的基础地址，不带 `/stream`，例如：

```text
VITE_CAMERA_BASE_URL=http://192.168.117.237
```

前端不会默认嵌入实时流。页面上的“打开快照”访问 `/capture`，“打开实时流”访问 `/stream`。YOLO 服务运行时建议优先看快照，不要同时长时间打开实时流，避免 ESP32-S3-CAM 的 `/stream` 被多路客户端占用。

“启动检测”和“停止检测”按钮调用后端 API，不会直接打开 Anaconda PowerShell Prompt。后端需要提前配置好 `VISION_PYTHON_COMMAND` 或 `VISION_CONDA_ENV`，详见 `backend/README.md`。多设备场景下，切换到不同设备后分别点击“启动检测”，后端会为每台设备启动独立 YOLO 进程。

## 验证流程

启动后端和前端后，浏览器打开前端页面，建议按以下顺序验证：

```text
1. 页面顶部显示 MQTT 已连接、SSE 实时。
2. 设备列表中出现 GLG-A-001；多设备时应同时出现 GLG-A-002 等设备。
3. 光照 D23，页面显示白天、亮度 0%。
4. 遮住 D23，页面显示夜晚，并根据 vision 显示 30% 或 100%。
5. 拖动亮度滑块到 59，点击下发亮度，LED 应进入 manual 且亮度约 59%。
6. 点击自动，页面和 telemetry 应恢复 mode:auto。
7. 手动 100% 点亮后拔掉 LED，故障状态应变成 open_load。
8. 接回 LED，故障状态应恢复 none。
```

如需查看后端原始状态，可同时打开：

```text
http://localhost:8080/api/devices
http://localhost:8080/api/vision/status
```

`/api/vision/status` 返回所有由后端托管的 YOLO 进程状态列表；页面会自动取当前选中设备对应的状态。

## 摄像头与 YOLO 注意事项

前端的摄像头按钮只是打开 ESP32-S3-CAM 的 HTTP 页面，不会把视频嵌入主页面长期占用连接：

```text
打开快照：/capture
打开实时流：/stream
打开首页：/
```

YOLO 服务运行时建议优先使用“打开快照”观察画面，不要长时间同时打开浏览器 `/stream` 和 YOLO 检测。部分 ESP32-S3-CAM 示例 HTTP 服务同一时间处理多路 MJPEG 流能力有限，多路占用可能导致 OpenCV 读取变慢或卡住。

如果摄像头 IP 改变，设备重新连接 MQTT 后会通过 `deviceInfo.cameraBaseUrl` 上报新地址；前端优先使用这个地址。`.env.local` 中的 `VITE_CAMERA_BASE_URL` 只作为兜底。

## 构建

```powershell
npm run build
```
