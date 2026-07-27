import json
import unittest

from vision.people_counter import PeopleSummary
from vision.vision_service import (
    VisionDayNightState,
    VisionPublishState,
    VisionStabilityState,
    build_telemetry_topic,
    build_vision_payload,
    build_vision_topic,
    parse_telemetry_dark,
    should_run_detection,
    should_publish,
    update_stable_summary,
    validate_stability_config,
)


class VisionServiceTest(unittest.TestCase):
    def test_build_vision_topic_uses_device_id(self):
        self.assertEqual(
            build_vision_topic("GLG-A-001"),
            "streetlight/GLG-A-001/vision",
        )

    def test_build_telemetry_topic_uses_device_id(self):
        self.assertEqual(
            build_telemetry_topic("GLG-A-001"),
            "streetlight/GLG-A-001/telemetry",
        )

    def test_build_vision_payload_contains_project_fields(self):
        summary = PeopleSummary(
            people_count=2,
            occupied=True,
            max_confidence=0.898,
        )

        payload = build_vision_payload(
            summary=summary,
            device_id="GLG-A-001",
            source="camera",
            timestamp_ms=123456789,
            frame_index=30,
        )

        self.assertEqual(payload["deviceId"], "GLG-A-001")
        self.assertEqual(payload["source"], "camera")
        self.assertEqual(payload["timestampMs"], 123456789)
        self.assertEqual(payload["frameIndex"], 30)
        self.assertEqual(payload["peopleCount"], 2)
        self.assertTrue(payload["occupied"])
        self.assertEqual(payload["maxConfidence"], 0.898)
        json.dumps(payload)

    def test_build_vision_payload_can_include_raw_and_stability_fields(self):
        stable = PeopleSummary(
            people_count=1,
            occupied=True,
            max_confidence=0.6,
        )
        raw = PeopleSummary(
            people_count=0,
            occupied=False,
            max_confidence=0.0,
        )
        stability_state = VisionStabilityState.create(window_size=5)
        stability_state.occupied_hits = 2
        stability_state.empty_hits = 3

        payload = build_vision_payload(
            summary=stable,
            device_id="GLG-A-001",
            source="camera",
            timestamp_ms=123,
            raw_summary=raw,
            stability_state=stability_state,
        )

        self.assertEqual(payload["peopleCount"], 1)
        self.assertEqual(payload["rawPeopleCount"], 0)
        self.assertFalse(payload["rawOccupied"])
        self.assertEqual(payload["stableWindow"], 5)
        self.assertEqual(payload["occupiedHits"], 2)
        self.assertEqual(payload["emptyHits"], 3)

    def test_should_publish_first_payload(self):
        state = VisionPublishState()

        self.assertTrue(should_publish(state, occupied=False, now_ms=1000, interval_ms=2000))

    def test_should_publish_when_occupied_changes(self):
        state = VisionPublishState(last_occupied=False, last_publish_ms=1000)

        self.assertTrue(should_publish(state, occupied=True, now_ms=1200, interval_ms=2000))

    def test_should_publish_when_interval_elapsed(self):
        state = VisionPublishState(last_occupied=True, last_publish_ms=1000)

        self.assertTrue(should_publish(state, occupied=True, now_ms=3000, interval_ms=2000))

    def test_should_not_publish_unchanged_before_interval(self):
        state = VisionPublishState(last_occupied=True, last_publish_ms=1000)

        self.assertFalse(should_publish(state, occupied=True, now_ms=1500, interval_ms=2000))

    def test_update_stable_summary_waits_for_enough_empty_samples(self):
        state = VisionStabilityState.create(window_size=5)
        empty = PeopleSummary(people_count=0, occupied=False, max_confidence=0.0)

        self.assertIsNone(update_stable_summary(state, empty, occupied_min_hits=2, empty_min_hits=4))
        self.assertIsNone(update_stable_summary(state, empty, occupied_min_hits=2, empty_min_hits=4))
        self.assertIsNone(update_stable_summary(state, empty, occupied_min_hits=2, empty_min_hits=4))

        stable = update_stable_summary(state, empty, occupied_min_hits=2, empty_min_hits=4)

        self.assertIsNotNone(stable)
        self.assertFalse(stable.occupied)
        self.assertEqual(stable.people_count, 0)

    def test_update_stable_summary_turns_occupied_on_quickly(self):
        state = VisionStabilityState.create(window_size=5)
        person = PeopleSummary(people_count=1, occupied=True, max_confidence=0.6)

        self.assertIsNone(update_stable_summary(state, person, occupied_min_hits=2, empty_min_hits=4))
        stable = update_stable_summary(state, person, occupied_min_hits=2, empty_min_hits=4)

        self.assertIsNotNone(stable)
        self.assertTrue(stable.occupied)
        self.assertEqual(stable.people_count, 1)
        self.assertEqual(stable.max_confidence, 0.6)

    def test_update_stable_summary_requires_more_empty_hits_to_clear_occupied(self):
        state = VisionStabilityState.create(window_size=5)
        person = PeopleSummary(people_count=1, occupied=True, max_confidence=0.6)
        empty = PeopleSummary(people_count=0, occupied=False, max_confidence=0.0)

        update_stable_summary(state, person, occupied_min_hits=2, empty_min_hits=4)
        stable = update_stable_summary(state, person, occupied_min_hits=2, empty_min_hits=4)
        self.assertTrue(stable.occupied)

        self.assertTrue(update_stable_summary(state, empty, occupied_min_hits=2, empty_min_hits=4).occupied)
        self.assertTrue(update_stable_summary(state, empty, occupied_min_hits=2, empty_min_hits=4).occupied)
        self.assertTrue(update_stable_summary(state, empty, occupied_min_hits=2, empty_min_hits=4).occupied)
        stable = update_stable_summary(state, empty, occupied_min_hits=2, empty_min_hits=4)

        self.assertFalse(stable.occupied)

    def test_validate_stability_config_rejects_impossible_thresholds(self):
        with self.assertRaises(ValueError):
            validate_stability_config(window_size=0, occupied_min_hits=1, empty_min_hits=1)

        with self.assertRaises(ValueError):
            validate_stability_config(window_size=5, occupied_min_hits=6, empty_min_hits=1)

        with self.assertRaises(ValueError):
            validate_stability_config(window_size=5, occupied_min_hits=1, empty_min_hits=6)

    def test_parse_telemetry_dark_reads_boolean_field(self):
        self.assertTrue(parse_telemetry_dark(b'{"dark":true}'))
        self.assertFalse(parse_telemetry_dark('{"dark":false}'))

    def test_parse_telemetry_dark_ignores_invalid_payload(self):
        self.assertIsNone(parse_telemetry_dark("not json"))
        self.assertIsNone(parse_telemetry_dark('{"dark":"false"}'))
        self.assertIsNone(parse_telemetry_dark('{"brightnessPercent":0}'))

    def test_should_run_detection_when_auto_pause_disabled(self):
        state = VisionDayNightState(dark=False)

        self.assertTrue(should_run_detection(False, state))

    def test_should_pause_detection_when_daytime_auto_pause_enabled(self):
        state = VisionDayNightState(dark=False)

        self.assertFalse(should_run_detection(True, state))

    def test_should_run_detection_only_after_night_telemetry(self):
        self.assertTrue(should_run_detection(True, VisionDayNightState(dark=True)))
        self.assertFalse(should_run_detection(True, VisionDayNightState(dark=None)))


if __name__ == "__main__":
    unittest.main()
