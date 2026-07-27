# 智能路灯 YOLOv8 行人检测

本目录用于智能路灯项目中的 AI 视觉检测部分，主要负责使用 YOLOv8 判断道路或小区场景中是否有人，并输出后续可接入 MQTT 的结构化结果。

## 目标

对街道、小区道路等图片或视频进行行人检测，输出简洁的 JSON 数据：

```json
{"deviceId":"GLG-A-001","source":"data/test_people_night.jpg","peopleCount":2,"occupied":true,"maxConfidence":0.876}
```

当前阶段支持本地图片检测、本地视频检测、批量素材检测，以及通过 MQTT 把 ESP32-S3-CAM 实时画面的行人检测结果发布给路灯节点。

## 测试素材建议

在本地创建素材目录：

```text
D:\esp\smart_streetlight\vision\data
```

建议使用以下文件名：

```text
test_people_day.jpg
test_people_night.jpg
test_empty_day.jpg
test_empty_night.jpg
test_people_night.mp4
test_empty_night.mp4
```

可以使用这些关键词搜索素材：

```text
night street pedestrian
community road people
sidewalk people night
empty street night
residential road pedestrian
```

建议从 Pexels、Pixabay、Unsplash 等授权说明清晰的网站获取素材，并记录素材来源链接，方便写课程设计报告。

## 运行环境

建议在已安装 YOLOv8、OpenCV 的 Anaconda 环境中运行：

```powershell
conda activate yolov8-env
cd D:\esp\smart_streetlight
```

可先检查环境：

```powershell
python -c "import ultralytics; print(ultralytics.__version__)"
python -c "import cv2; print(cv2.__version__)"
```

## 单张图片检测

```powershell
python -m vision.vision_detect --source vision\data\test_people_night.jpg --output vision\output\people_night_result.jpg
```

## 单个视频检测

```powershell
python -m vision.vision_detect --source vision\data\test_people_night.mp4 --output vision\output\people_night_result.mp4
```

`vision_detect` 会把标注后的视频保存到 `--output` 指定路径。默认 `--frame-stride 5`，即每 5 帧做一次 YOLO 检测，其余帧原样写入，速度比逐帧检测快很多。

如果要对整段视频逐帧检测，显式指定 `--frame-stride 1`：

```powershell
python -m vision.vision_detect --source vision\data\test_people_night.mp4 --output vision\output\people_night_full_result.mp4 --frame-stride 1
```

如果视频较长，逐帧检测会比较慢。课程展示素材通常可以先用跳帧检测：

```powershell
python -m vision.vision_detect --source vision\data\video9.mp4 --output vision\output\video9_result_fast.mp4 --model yolov8n.pt --frame-stride 5
```

如果只是快速验证脚本和模型是否可用，可以临时限制处理前 60 帧：

```powershell
python -m vision.vision_detect --source vision\data\test_people_night.mp4 --output vision\output\people_night_result.mp4 --max-frames 60
```

如果要快速预览 `video9.mp4` 的前 300 帧：

```powershell
python -m vision.vision_detect --source vision\data\video9.mp4 --output vision\output\video9_result_preview.mp4 --model yolov8n.pt --frame-stride 5 --max-frames 300
```

注意：`vision.vision_service` 用于实时检测和 MQTT 发布，不会保存标注后的视频文件；保存本地视频请使用 `vision.vision_detect`。

## 批量检测

批量检测会自动扫描 `vision\data` 目录下支持的图片和视频：

```powershell
python -m vision.vision_detect --source vision\data --output vision\output --max-frames 60
```

批量模式会生成标注后的图片/视频，以及两个汇总报告：

```text
vision\output\vision_report.json
vision\output\vision_report.csv
```

`--max-frames 60` 只适合快速验证视频检测效果。正式处理完整视频时，应去掉该参数；如果希望每一帧都参与 YOLO 检测，再加上 `--frame-stride 1`。

## 输出字段说明

```text
deviceId       设备编号，默认 GLG-A-001
source         输入图片或视频路径
peopleCount    检测到的行人数
occupied       是否有人，peopleCount > 0 时为 true
maxConfidence  当前图片或视频帧中行人的最高置信度
frameIndex     视频帧序号，仅视频检测结果中出现
output         标注结果文件路径
```

## 后续集成方向

批量检测主要用于离线验证、报告截图和置信度阈值调试。真正的摄像头实时检测应作为常驻服务运行：

```text
ESP32-S3-CAM HTTP 视频流 -> YOLOv8 视觉服务 -> MQTT vision 主题 -> 路灯控制策略
```

第一版实时检测建议参数：

```text
模型：yolov8n.pt
视频流分辨率：320x240 或 640x480
检测间隔：每 0.5-1.0 秒检测 1 帧
稳定判定：5 次检测中至少 2 次有人才认为有人，至少 4 次无人再认为无人
MQTT 发布间隔：状态变化立即发布，同状态每 2 秒左右发布一次心跳
白天优化：可订阅 telemetry，在 dark:false 时暂停 YOLO，dark:true 时自动恢复
```

后续可新增 MQTT 发布主题：

```text
streetlight/GLG-A-001/vision
```

## 实时视觉服务

实时服务会读取摄像头、视频文件或 HTTP 视频流，先对 YOLO 单帧结果做稳定窗口判定，再把稳定后的识别结果发布到 EMQX。默认只发布 ESP32 需要的短消息，避免消息过长被固件丢弃。

如果加上 `--auto-pause-by-dark`，实时服务会额外订阅 `streetlight/GLG-A-001/telemetry`：

```text
dark:false -> 关闭本地 VideoCapture，暂停读取 /stream、暂停 YOLO 推理、暂停发布 vision
dark:true  -> 重新打开 /stream，恢复 YOLO 推理和 vision 发布
```

首次还没收到 telemetry 前，服务会先保持检测，避免因为 MQTT 订阅尚未收到第一条状态而卡住。

先安装 MQTT 客户端依赖：

```powershell
conda activate yolov8-env
pip install paho-mqtt
```

用本地视频做 MQTT 发布测试：

```powershell
cd D:\esp\smart_streetlight
python -m vision.vision_service --source vision\data\test_people_night.mp4 --max-detected-frames 5 --host your-emqx-host --port 8883 --tls --username your-username --password your-password
```

上面命令只用于快速验证 MQTT 链路。若要让实时服务检测完整视频，去掉 `--max-detected-frames`：

```powershell
python -m vision.vision_service --source vision\data\test_people_night.mp4 --conf 0.35 --frame-stride 15 --publish-interval-ms 2000 --stable-window 5 --occupied-min-hits 2 --empty-min-hits 4 --host your-emqx-host --port 8883 --tls --username your-username --password your-password
```

如果希望调试完整逐帧检测过程，可把检测步长设为 1；如果还把发布间隔设为 0，会对每次检测都发布 MQTT 消息，MQTTX/EMQX 中消息数量会非常多：

```powershell
python -m vision.vision_service --source vision\data\test_people_night.mp4 --frame-stride 1 --publish-interval-ms 0 --host your-emqx-host --port 8883 --tls --username your-username --password your-password
```

如果使用电脑摄像头：

```powershell
python -m vision.vision_service --source 0 --conf 0.35 --frame-stride 15 --publish-interval-ms 2000 --stable-window 5 --occupied-min-hits 2 --empty-min-hits 4 --host your-emqx-host --port 8883 --tls --username your-username --password your-password
```

如果后续 ESP32-S3-CAM 已经提供 HTTP 视频流：

```powershell
python -m vision.vision_service --source http://ESP32_CAM_IP/stream --conf 0.35 --frame-stride 15 --publish-interval-ms 2000 --stable-window 5 --occupied-min-hits 2 --empty-min-hits 4 --auto-pause-by-dark --host your-emqx-host --port 8883 --tls --username your-username --password your-password
```

可在 MQTTX 中订阅下面的主题检查结果：

```text
streetlight/GLG-A-001/vision
```

示例消息：

```json
{"deviceId":"GLG-A-001","source":"http://ESP32_CAM_IP/stream","timestampMs":123456789,"peopleCount":1,"occupied":true,"maxConfidence":0.612,"frameIndex":30}
```

如果需要排查 YOLO 单帧结果和稳定窗口计数，可临时加上 `--include-debug-fields`，消息中会额外包含 `rawPeopleCount`、`rawOccupied`、`stableWindow`、`occupiedHits`、`emptyHits` 等字段。正常演示时建议不加。

然后更新 ESP32 路灯控制策略：

```text
白天：关闭路灯
夜间有人：高亮度
夜间无人：低亮度节能
手动模式：平台下行命令优先
```
