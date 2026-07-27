import argparse
import csv
import json
from pathlib import Path

from vision.people_counter import DetectionBox, summarize_people, to_vision_payload

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
VIDEO_EXTENSIONS = {".mp4", ".avi", ".mov", ".mkv"}
SUPPORTED_EXTENSIONS = IMAGE_EXTENSIONS | VIDEO_EXTENSIONS


def _load_yolo(model_path: str):
    try:
        from ultralytics import YOLO
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "ultralytics is not installed in this Python environment. "
            "Run this script with the Python environment where YOLOv8 is installed."
        ) from exc

    return YOLO(model_path)


def _load_cv2():
    try:
        import cv2
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "opencv-python is not installed in this Python environment. "
            "Run this script with the YOLOv8 environment or install opencv-python."
        ) from exc

    return cv2


def _detection_boxes_from_result(result) -> list[DetectionBox]:
    boxes = []
    for box in result.boxes:
        boxes.append(
            DetectionBox(
                class_id=int(box.cls[0].item()),
                confidence=float(box.conf[0].item()),
            )
        )
    return boxes


def _print_payload(payload: dict) -> None:
    print(json.dumps(payload, ensure_ascii=False))


def collect_sources(source_dir: Path) -> list[Path]:
    return sorted(
        [
            path
            for path in source_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in SUPPORTED_EXTENSIONS
        ],
        key=lambda path: str(path).lower(),
    )


def build_batch_output_path(source: Path, output_dir: Path) -> Path:
    suffix = source.suffix.lower()
    return output_dir / f"{source.stem}_result{suffix}"


def write_batch_reports(rows: list[dict], output_dir: Path) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "vision_report.json"
    csv_path = output_dir / "vision_report.csv"

    json_path.write_text(
        json.dumps(rows, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    fieldnames = ["deviceId", "source", "peopleCount", "occupied", "maxConfidence", "output"]
    with csv_path.open("w", encoding="utf-8", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})

    return json_path, csv_path


def should_stop_before_frame(next_frame_index: int, max_frames: int) -> bool:
    return max_frames > 0 and next_frame_index > max_frames


def detect_image(args: argparse.Namespace) -> dict:
    cv2 = _load_cv2()
    model = _load_yolo(args.model)
    source = Path(args.source)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)

    results = model(str(source), conf=args.conf)
    result = results[0]
    summary = summarize_people(_detection_boxes_from_result(result), args.conf)

    annotated_img = result.plot()
    cv2.imwrite(str(output), annotated_img)

    payload = to_vision_payload(summary, args.device_id, str(source))
    payload["output"] = str(output)
    _print_payload(payload)

    if args.show:
        cv2.imshow("Smart Streetlight Vision", annotated_img)
        cv2.waitKey(0)
        cv2.destroyAllWindows()

    return payload


def detect_video(args: argparse.Namespace) -> dict:
    cv2 = _load_cv2()
    model = _load_yolo(args.model)
    cap = cv2.VideoCapture(args.source)
    if not cap.isOpened():
        raise RuntimeError(f"cannot open video source: {args.source}")

    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    if fps <= 0:
        fps = 20.0

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(str(output), fourcc, fps, (width, height))

    frame_index = 0
    last_payload = None

    while cap.isOpened():
        if should_stop_before_frame(frame_index + 1, args.max_frames):
            break

        ok, frame = cap.read()
        if not ok:
            break

        frame_index += 1
        if args.frame_stride > 1 and (frame_index - 1) % args.frame_stride != 0:
            writer.write(frame)
            continue

        results = model(frame, conf=args.conf, verbose=False)
        result = results[0]
        summary = summarize_people(_detection_boxes_from_result(result), args.conf)
        annotated_frame = result.plot()
        writer.write(annotated_frame)

        payload = to_vision_payload(summary, args.device_id, args.source, frame_index)
        payload["output"] = str(output)
        _print_payload(payload)
        last_payload = payload

        if args.show:
            cv2.imshow("Smart Streetlight Vision", annotated_frame)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break

    cap.release()
    writer.release()
    if args.show:
        cv2.destroyAllWindows()

    if last_payload is None:
        payload = to_vision_payload(
            summarize_people([], args.conf),
            args.device_id,
            args.source,
            frame_index=0,
        )
        payload["output"] = str(output)
        _print_payload(payload)
        return payload

    return last_payload


def detect_one(args: argparse.Namespace) -> dict:
    source_path = Path(args.source)
    is_image = source_path.suffix.lower() in IMAGE_EXTENSIONS

    if is_image:
        return detect_image(args)

    return detect_video(args)


def detect_batch(args: argparse.Namespace) -> list[dict]:
    source_dir = Path(args.source)
    output_dir = Path(args.output or "vision_output")
    sources = collect_sources(source_dir)
    if not sources:
        raise RuntimeError(f"no supported image/video files found in: {source_dir}")

    rows = []
    for source in sources:
        child_args = argparse.Namespace(**vars(args))
        child_args.source = str(source)
        child_args.output = str(build_batch_output_path(source, output_dir))
        child_args.show = False
        rows.append(detect_one(child_args))

    json_path, csv_path = write_batch_reports(rows, output_dir)
    print(f"batch reports saved: {json_path}, {csv_path}")
    return rows


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run YOLOv8 person detection for the smart streetlight project."
    )
    parser.add_argument("--source", required=True, help="Image, video, camera index, or stream URL.")
    parser.add_argument("--device-id", default="GLG-A-001", help="Device ID used in JSON output.")
    parser.add_argument("--model", default="yolov8n.pt", help="YOLO model path.")
    parser.add_argument("--conf", type=float, default=0.25, help="Confidence threshold.")
    parser.add_argument("--output", default="", help="Annotated output file path.")
    parser.add_argument("--show", action="store_true", help="Show annotated frames in an OpenCV window.")
    parser.add_argument(
        "--frame-stride",
        type=int,
        default=5,
        help="Run YOLO once every N video frames to reduce CPU load.",
    )
    parser.add_argument(
        "--max-frames",
        type=int,
        default=0,
        help="Stop after N frames for quick tests. 0 means no limit.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    source_path = Path(args.source)
    if source_path.is_dir():
        if not args.output:
            args.output = "vision/output"
        detect_batch(args)
        return

    is_image = source_path.suffix.lower() in IMAGE_EXTENSIONS

    if not args.output:
        args.output = "vision_output.jpg" if is_image else "vision_output.mp4"

    detect_one(args)


if __name__ == "__main__":
    main()
