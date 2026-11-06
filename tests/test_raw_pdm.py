import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
from raw_pdm import analyse, read_hex


class Port:
    def __init__(self, lines):
        self.lines = iter(lines)

    def readline(self):
        return next(self.lines)


class RawPDMTests(unittest.TestCase):
    def test_constant_polarity(self):
        for word, sample in [(0xffffffff, 32767), (0, -32768)]:
            result = analyse([word]*6144, [sample]*2048, 1536000, 96)
            self.assertEqual(result["pcm_max_error_after_settling"], 0)
            self.assertEqual(result["frames"], 512)

    def test_idle_alternation(self):
        result = analyse([0xf0f0f0f0]*6144, [0]*2048, 1536000, 96)
        self.assertEqual(result["pcm_max_error_after_settling"], 0)
        for channel in result["channels"]:
            self.assertEqual(channel["ones_fraction"], 0.5)
            self.assertEqual(channel["transition_fraction"], 1)

    def test_mismatched_pcm(self):
        pcm = [0]*2048
        pcm[400] = 17
        result = analyse([0xf0f0f0f0]*6144, pcm, 1536000, 96)
        self.assertEqual(result["pcm_max_error_after_settling"], 17)

    def test_real_trace(self):
        path = Path(__file__).parents[1] / "build-tests/raw-external-1000/raw.json"
        if not path.exists():
            self.skipTest("Hardware trace unavailable")
        data = json.loads(path.read_text())
        result = analyse(data["words"], data["pcm"], data["pdm_hz"], data["ratio"])
        self.assertEqual(result["pcm_max_error_after_settling"], 0)

    def test_export_rejects_truncation(self):
        with self.assertRaises(ValueError):
            read_hex(Port([b"12345678\n", b"RAW_END\n"]), "RAW_END", 8, 2)

    def test_export_rejects_malformed(self):
        with self.assertRaises(ValueError):
            read_hex(Port([b"nothex\n"]), "RAW_END", 8, 1)


if __name__ == "__main__":
    unittest.main()
