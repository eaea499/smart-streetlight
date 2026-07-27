import unittest

from vision.people_counter import DetectionBox, summarize_people, to_vision_payload


class PeopleCounterTest(unittest.TestCase):
    def test_counts_only_people_above_confidence_threshold(self):
        boxes = [
            DetectionBox(class_id=0, confidence=0.91),
            DetectionBox(class_id=0, confidence=0.62),
            DetectionBox(class_id=2, confidence=0.95),
            DetectionBox(class_id=0, confidence=0.18),
        ]

        summary = summarize_people(boxes, confidence_threshold=0.25)

        self.assertEqual(summary.people_count, 2)
        self.assertTrue(summary.occupied)
        self.assertAlmostEqual(summary.max_confidence, 0.91)

    def test_no_people_reports_unoccupied(self):
        boxes = [
            DetectionBox(class_id=1, confidence=0.88),
            DetectionBox(class_id=3, confidence=0.74),
        ]

        summary = summarize_people(boxes, confidence_threshold=0.25)

        self.assertEqual(summary.people_count, 0)
        self.assertFalse(summary.occupied)
        self.assertEqual(summary.max_confidence, 0.0)

    def test_payload_matches_project_mqtt_shape(self):
        summary = summarize_people([DetectionBox(class_id=0, confidence=0.77)])

        payload = to_vision_payload(
            summary,
            device_id="GLG-A-001",
            source="test.jpg",
            frame_index=3,
        )

        self.assertEqual(payload["deviceId"], "GLG-A-001")
        self.assertEqual(payload["source"], "test.jpg")
        self.assertEqual(payload["frameIndex"], 3)
        self.assertEqual(payload["peopleCount"], 1)
        self.assertTrue(payload["occupied"])
        self.assertEqual(payload["maxConfidence"], 0.77)


if __name__ == "__main__":
    unittest.main()
