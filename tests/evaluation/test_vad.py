import unittest
from argparse import Namespace

from vad import run, speech_coverage


class SpeechCoverageTest(unittest.TestCase):
    def test_invalid_thresholds_and_labels_fail_before_inference(self):
        for threshold in (0, 1, -1, float("inf"), float("nan")):
            with self.assertRaisesRegex(ValueError, "threshold"):
                run(Namespace(label="valid", threshold=threshold))
        for label in ("..", ".", "../outside", "/outside"):
            with self.assertRaisesRegex(ValueError, "label"):
                run(Namespace(label=label, threshold=.5))

    def test_overlap_is_union_speech_not_speaker_seconds(self):
        reference = [{"start": 0, "end": 2}, {"start": 1, "end": 3}]
        result = speech_coverage(reference, [{"start": 0, "end": 3}], 4)
        self.assertEqual(result["reference_speech_seconds"], 3)
        self.assertEqual(result["recall"], 1)
        self.assertEqual(result["nonspeech_coverage"], 0)

    def test_missing_speech_and_noise_are_separate(self):
        result = speech_coverage([{"start": 1, "end": 3}], [{"start": 0, "end": 2}], 4)
        self.assertEqual(result["recall"], .5)
        self.assertEqual(result["missed_speech_seconds"], 1)
        self.assertEqual(result["false_alarm_seconds"], 1)
        self.assertEqual(result["nonspeech_coverage"], .5)

    def test_silence_is_not_perfect_recall(self):
        result = speech_coverage([], [{"start": 0, "end": 1}], 2)
        self.assertIsNone(result["recall"])
        self.assertEqual(result["false_alarm_seconds"], 1)
        self.assertEqual(result["precision"], 0)

    def test_empty_detection_is_a_miss(self):
        result = speech_coverage([{"start": 0, "end": 2}], [], 2)
        self.assertEqual(result["recall"], 0)
        self.assertIsNone(result["precision"])
        self.assertIsNone(result["nonspeech_coverage"])

    def test_invalid_intervals_fail(self):
        for span in ({"start": -1, "end": 1}, {"start": 0, "end": float("nan")},
                     {"start": 0, "end": 3}):
            with self.assertRaises(ValueError):
                speech_coverage([], [span], 2)


if __name__ == "__main__":
    unittest.main()
