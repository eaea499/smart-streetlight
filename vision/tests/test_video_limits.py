import unittest

from vision.vision_detect import should_stop_before_frame


class VideoLimitsTest(unittest.TestCase):
    def test_does_not_stop_when_limit_is_disabled(self):
        self.assertFalse(should_stop_before_frame(next_frame_index=100, max_frames=0))

    def test_stops_before_reading_past_max_frames(self):
        self.assertFalse(should_stop_before_frame(next_frame_index=60, max_frames=60))
        self.assertTrue(should_stop_before_frame(next_frame_index=61, max_frames=60))


if __name__ == "__main__":
    unittest.main()
