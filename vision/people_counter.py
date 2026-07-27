from dataclasses import dataclass
from typing import Iterable

PERSON_CLASS_ID = 0


@dataclass(frozen=True)
class DetectionBox:
    class_id: int
    confidence: float


@dataclass(frozen=True)
class PeopleSummary:
    people_count: int
    occupied: bool
    max_confidence: float


def summarize_people(
    boxes: Iterable[DetectionBox],
    confidence_threshold: float = 0.25,
) -> PeopleSummary:
    people_confidences = [
        box.confidence
        for box in boxes
        if box.class_id == PERSON_CLASS_ID and box.confidence >= confidence_threshold
    ]

    people_count = len(people_confidences)
    max_confidence = max(people_confidences) if people_confidences else 0.0

    return PeopleSummary(
        people_count=people_count,
        occupied=people_count > 0,
        max_confidence=round(max_confidence, 3),
    )


def to_vision_payload(
    summary: PeopleSummary,
    device_id: str,
    source: str,
    frame_index: int | None = None,
) -> dict:
    payload = {
        "deviceId": device_id,
        "source": source,
        "peopleCount": summary.people_count,
        "occupied": summary.occupied,
        "maxConfidence": summary.max_confidence,
    }

    if frame_index is not None:
        payload["frameIndex"] = frame_index

    return payload
