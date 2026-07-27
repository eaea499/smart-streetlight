import subprocess
import sys
import unittest


class VisionCliTest(unittest.TestCase):
    def test_help_does_not_require_ultralytics_runtime(self):
        result = subprocess.run(
            [sys.executable, "-m", "vision.vision_detect", "--help"],
            cwd=".",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--source", result.stdout)
        self.assertIn("--device-id", result.stdout)


if __name__ == "__main__":
    unittest.main()
