import csv
import json
import unittest
import uuid
from pathlib import Path

from vision.vision_detect import (
    build_batch_output_path,
    collect_sources,
    write_batch_reports,
)


class BatchDetectionTest(unittest.TestCase):
    def setUp(self):
        self.temp_root = Path(".test_tmp") / f"{self._testMethodName}_{uuid.uuid4().hex}"
        self.temp_root.mkdir(parents=True)

    def test_collect_sources_returns_supported_files_in_stable_order(self):
        root = self.temp_root
        (root / "b.mp4").write_text("video")
        (root / "a.jpg").write_text("image")
        (root / "notes.txt").write_text("ignore")
        (root / "nested").mkdir()
        (root / "nested" / "c.png").write_text("image")

        sources = collect_sources(root)

        self.assertEqual(
            [path.name for path in sources],
            ["a.jpg", "b.mp4", "c.png"],
        )

    def test_build_batch_output_path_adds_result_suffix(self):
        output_path = build_batch_output_path(
            Path("vision/data/test_people_night.jpg"),
            Path("vision/output"),
        )

        self.assertEqual(output_path, Path("vision/output/test_people_night_result.jpg"))

    def test_write_batch_reports_creates_json_and_csv(self):
        output_dir = self.temp_root
        rows = [
            {
                "deviceId": "GLG-A-001",
                "source": "vision/data/a.jpg",
                "peopleCount": 2,
                "occupied": True,
                "maxConfidence": 0.88,
                "output": "vision/output/a_result.jpg",
            },
            {
                "deviceId": "GLG-A-001",
                "source": "vision/data/b.jpg",
                "peopleCount": 0,
                "occupied": False,
                "maxConfidence": 0.0,
                "output": "vision/output/b_result.jpg",
            },
        ]

        json_path, csv_path = write_batch_reports(rows, output_dir)

        loaded_json = json.loads(json_path.read_text(encoding="utf-8"))
        self.assertEqual(loaded_json, rows)

        with csv_path.open("r", encoding="utf-8", newline="") as csv_file:
            loaded_csv = list(csv.DictReader(csv_file))

        self.assertEqual(loaded_csv[0]["source"], "vision/data/a.jpg")
        self.assertEqual(loaded_csv[0]["peopleCount"], "2")
        self.assertEqual(loaded_csv[1]["occupied"], "False")


if __name__ == "__main__":
    unittest.main()
