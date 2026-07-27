import argparse
import json
import time
from collections import deque
from dataclasses import dataclass

from vision.people_counter import PeopleSummary, summarize_people
from vision.vision_detect import _detection_boxes_from_result, _load_cv2, _load_yolo


@dataclass
class VisionPublishState:
    last_occupied: bool | None = None
    last_publish_ms: int | None = None


@dataclass
class VisionStabilityState:
    samples: deque[PeopleSummary]
    stable_summary: PeopleSummary | None = None
    occupied_hits: int = 0
    empty_hits: int = 0

    @classmethod
    def create(cls, window_size: int) -> "VisionStabilityState":
        return cls(samples=deque(maxlen=window_size))


@dataclass
class VisionDayNightState:
    dark: bool | None = None
    last_telemetry_ms: int | None = None


def build_vision_topic(device_id: str) -> str:
    return f"streetlight/{device_id}/vision"


def build_telemetry_topic(device_id: str) -> str:
    return f"streetlight/{device_id}/telemetry"


def now_ms() -> int:
    return int(time.time() * 1000)


def build_vision_payload(
    summary: PeopleSummary,
    device_id: str,
    source: str,
    timestamp_ms: int,
    frame_index: int | None = None,
    raw_summary: PeopleSummary | None = None,
    stability_state: VisionStabilityState | None = None,
) -> dict:
    payload = {
        "deviceId": device_id,
        "source": source,
        "timestampMs": timestamp_ms,
        "peopleCount": summary.people_count,
        "occupied": summary.occupied,
        "maxConfidence": summary.max_confidence,
    }

    if frame_index is not None:
        payload["frameIndex"] = frame_index

    if raw_summary is not None:
        payload["rawPeopleCount"] = raw_summary.people_count
        payload["rawOccupied"] = raw_summary.occupied
        payload["rawMaxConfidence"] = raw_summary.max_confidence

    if stability_state is not None:
        payload["stableWindow"] = stability_state.samples.maxlen
        payload["occupiedHits"] = stability_state.occupied_hits
        payload["emptyHits"] = stability_state.empty_hits

    return payload


def validate_stability_config(
    window_size: int,
    occupied_min_hits: int,
    empty_min_hits: int,
) -> None:
    if window_size < 1:
        raise ValueError("--stable-window must be at least 1")
    if occupied_min_hits < 1 or occupied_min_hits > window_size:
        raise ValueError("--occupied-min-hits must be between 1 and --stable-window")
    if empty_min_hits < 1 or empty_min_hits > window_size:
        raise ValueError("--empty-min-hits must be between 1 and --stable-window")


def update_stable_summary(
    state: VisionStabilityState,
    raw_summary: PeopleSummary,
    occupied_min_hits: int,
    empty_min_hits: int,
) -> PeopleSummary | None:
    state.samples.append(raw_summary)
    state.occupied_hits = sum(1 for sample in state.samples if sample.occupied)
    state.empty_hits = len(state.samples) - state.occupied_hits

    if state.occupied_hits >= occupied_min_hits:
        occupied_samples = [sample for sample in state.samples if sample.occupied]
        state.stable_summary = PeopleSummary(
            people_count=max(sample.people_count for sample in occupied_samples),
            occupied=True,
            max_confidence=max(sample.max_confidence for sample in occupied_samples),
        )
    elif state.empty_hits >= empty_min_hits:
        state.stable_summary = PeopleSummary(
            people_count=0,
            occupied=False,
            max_confidence=0.0,
        )

    return state.stable_summary


def parse_telemetry_dark(payload: bytes | str) -> bool | None:
    if isinstance(payload, bytes):
        payload = payload.decode("utf-8", errors="replace")

    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        return None

    dark = data.get("dark")
    return dark if isinstance(dark, bool) else None


def should_run_detection(
    auto_pause_by_dark: bool,
    day_night_state: VisionDayNightState,
) -> bool:
    if not auto_pause_by_dark:
        return True
    return day_night_state.dark is True


def should_publish(
    state: VisionPublishState,
    occupied: bool,
    now_ms: int,
    interval_ms: int,
) -> bool:
    if state.last_publish_ms is None or state.last_occupied is None:
        return True
    if occupied != state.last_occupied:
        return True
    return now_ms - state.last_publish_ms >= interval_ms


def update_publish_state(
    state: VisionPublishState,
    occupied: bool,
    publish_ms: int,
) -> None:
    state.last_occupied = occupied
    state.last_publish_ms = publish_ms


def _load_mqtt_client_class():
    try:
        import paho.mqtt.client as mqtt
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "paho-mqtt is not installed. Install it in yolov8-env with: "
            "pip install paho-mqtt"
        ) from exc

    return mqtt.Client


def configure_telemetry_subscription(client, args: argparse.Namespace, day_night_state: VisionDayNightState) -> None:
    telemetry_topic = build_telemetry_topic(args.device_id)

    def on_connect(client, userdata, flags, rc):
        if rc != 0:
            print(f"mqtt connect failed, rc={rc}")
            return
        client.subscribe(telemetry_topic, qos=0)
        print(f"telemetry subscription enabled, topic={telemetry_topic}")

    def on_message(client, userdata, msg):
        dark = parse_telemetry_dark(msg.payload)
        if dark is None:
            return

        previous_dark = day_night_state.dark
        day_night_state.dark = dark
        day_night_state.last_telemetry_ms = now_ms()

        if previous_dark != dark:
            state_text = "night, detection enabled" if dark else "daytime, detection paused"
            print(f"telemetry dark={str(dark).lower()} ({state_text})")

    client.on_connect = on_connect
    client.on_message = on_message


def create_mqtt_client(args: argparse.Namespace, day_night_state: VisionDayNightState | None = None):
    client_class = _load_mqtt_client_class()
    client = client_class(client_id=args.client_id)

    if day_night_state is not None:
        configure_telemetry_subscription(client, args, day_night_state)

    if args.username:
        client.username_pw_set(args.username, args.password)

    if args.tls:
        client.tls_set(ca_certs=args.ca_cert or None)

    client.connect(args.host, args.port, keepalive=args.keepalive)
    client.loop_start()
    return client


def parse_source(source: str):
    if source.isdigit():
        return int(source)
    return source


def is_live_source(source: str) -> bool:
    return source.startswith(("http://", "https://", "rtsp://", "rtmp://"))


def open_video_capture(cv2, source: str):
    cap = cv2.VideoCapture(parse_source(source))
    return cap if cap.isOpened() else None


def close_video_capture(cap) -> None:
    if cap is not None:
        cap.release()


def run_service(args: argparse.Namespace) -> None:
    validate_stability_config(
        args.stable_window,
        args.occupied_min_hits,
        args.empty_min_hits,
    )

    cv2 = _load_cv2()
    model = _load_yolo(args.model)
    day_night_state = VisionDayNightState()
    mqtt_client = create_mqtt_client(
        args,
        day_night_state if args.auto_pause_by_dark else None,
    )
    topic = build_vision_topic(args.device_id)

    state = VisionPublishState()
    stability_state = VisionStabilityState.create(args.stable_window)
    frame_index = 0
    detected_frame_count = 0
    cap = None
    pause_reason: str | None = None
    should_retry_source = args.auto_pause_by_dark or is_live_source(args.source)

    print(f"vision service started, source={args.source}, topic={topic}")
    try:
        while True:
            if not should_run_detection(args.auto_pause_by_dark, day_night_state):
                if cap is not None:
                    close_video_capture(cap)
                    cap = None
                next_pause_reason = "waiting-telemetry" if day_night_state.dark is None else "daytime"
                if pause_reason != next_pause_reason:
                    if next_pause_reason == "waiting-telemetry":
                        print("waiting for telemetry dark value, pause YOLO detection")
                    else:
                        print("daytime detected, close video stream and pause YOLO detection")
                    pause_reason = next_pause_reason
                time.sleep(args.day_poll_interval_ms / 1000.0)
                continue

            if pause_reason is not None:
                print("night detected, reopen video stream and resume YOLO detection")
                state = VisionPublishState()
                stability_state = VisionStabilityState.create(args.stable_window)
                frame_index = 0
                pause_reason = None

            if cap is None:
                cap = open_video_capture(cv2, args.source)
                if cap is None:
                    if should_retry_source:
                        print(f"cannot open video source: {args.source}, retrying...")
                        time.sleep(args.reconnect_interval_ms / 1000.0)
                        continue
                    raise RuntimeError(f"cannot open video source: {args.source}")

            ok, frame = cap.read()
            if not ok:
                close_video_capture(cap)
                cap = None
                if should_retry_source:
                    print(f"video source unavailable or ended: {args.source}, retrying...")
                    time.sleep(args.reconnect_interval_ms / 1000.0)
                    continue
                break

            frame_index += 1
            if args.frame_stride > 1 and (frame_index - 1) % args.frame_stride != 0:
                continue

            results = model(frame, conf=args.conf, verbose=False)
            raw_summary = summarize_people(_detection_boxes_from_result(results[0]), args.conf)
            stable_summary = update_stable_summary(
                stability_state,
                raw_summary,
                args.occupied_min_hits,
                args.empty_min_hits,
            )

            if stable_summary is None:
                detected_frame_count += 1
                if args.max_detected_frames > 0 and detected_frame_count >= args.max_detected_frames:
                    break
                continue

            timestamp_ms = now_ms()
            payload = build_vision_payload(
                summary=stable_summary,
                device_id=args.device_id,
                source=args.source,
                timestamp_ms=timestamp_ms,
                frame_index=frame_index,
                raw_summary=raw_summary if args.include_debug_fields else None,
                stability_state=stability_state if args.include_debug_fields else None,
            )

            if should_publish(state, stable_summary.occupied, timestamp_ms, args.publish_interval_ms):
                payload_text = json.dumps(payload, ensure_ascii=False)
                mqtt_client.publish(topic, payload_text, qos=args.qos, retain=False)
                update_publish_state(state, stable_summary.occupied, timestamp_ms)
                print(payload_text)

            detected_frame_count += 1
            if args.max_detected_frames > 0 and detected_frame_count >= args.max_detected_frames:
                break

            if args.show:
                annotated_frame = results[0].plot()
                cv2.imshow("Smart Streetlight Vision Service", annotated_frame)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break
    finally:
        close_video_capture(cap)
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        if args.show:
            cv2.destroyAllWindows()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run YOLOv8 real-time person detection and publish MQTT vision data."
    )
    parser.add_argument("--source", required=True, help="Camera index, video file, or stream URL.")
    parser.add_argument("--device-id", default="GLG-A-001", help="Device ID used in MQTT topic and payload.")
    parser.add_argument("--model", default="yolov8n.pt", help="YOLO model path.")
    parser.add_argument("--conf", type=float, default=0.25, help="Confidence threshold.")
    parser.add_argument("--frame-stride", type=int, default=15, help="Run YOLO once every N frames.")
    parser.add_argument("--publish-interval-ms", type=int, default=2000, help="Minimum publish interval.")
    parser.add_argument("--stable-window", type=int, default=5, help="Number of recent detections used for stable occupancy.")
    parser.add_argument("--occupied-min-hits", type=int, default=2, help="People detections needed in the stable window.")
    parser.add_argument("--empty-min-hits", type=int, default=4, help="Empty detections needed in the stable window.")
    parser.add_argument("--include-debug-fields", action="store_true", help="Publish raw detection and stability counters.")
    parser.add_argument("--auto-pause-by-dark", action="store_true", help="Subscribe to telemetry and pause YOLO while dark is false.")
    parser.add_argument("--day-poll-interval-ms", type=int, default=1000, help="Sleep interval while daytime pause is active.")
    parser.add_argument("--reconnect-interval-ms", type=int, default=2000, help="Retry interval when a live video source is unavailable.")
    parser.add_argument("--max-detected-frames", type=int, default=0, help="Stop after N detected frames. 0 means no limit.")
    parser.add_argument("--show", action="store_true", help="Show annotated frames.")

    parser.add_argument("--host", required=True, help="MQTT broker host.")
    parser.add_argument("--port", type=int, default=8883, help="MQTT broker port.")
    parser.add_argument("--client-id", default="vision_service_GLG_A_001", help="MQTT client ID.")
    parser.add_argument("--username", default="", help="MQTT username.")
    parser.add_argument("--password", default="", help="MQTT password.")
    parser.add_argument("--tls", action="store_true", help="Enable MQTT TLS.")
    parser.add_argument("--ca-cert", default="", help="Optional CA certificate path.")
    parser.add_argument("--keepalive", type=int, default=60, help="MQTT keepalive seconds.")
    parser.add_argument("--qos", type=int, default=0, choices=[0, 1, 2], help="MQTT publish QoS.")
    return parser.parse_args()


def main() -> None:
    run_service(parse_args())


if __name__ == "__main__":
    main()
