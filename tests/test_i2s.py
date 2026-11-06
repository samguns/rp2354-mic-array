import json
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
from verify_audio import decode_i2s, validate_tone


def fixture(audio_format="i2s"):
    triples = []
    for word_index in range(512):
        frame = word_index // 2
        sample = int(round(512 * math.sin(2 * math.pi * frame / 32)) * min(frame, 256) / 256)
        value = (sample & 65535) << 16
        channel = word_index % 2
        for bit in range(31, -1, -1):
            lr = channel
            if audio_format == "i2s" and bit == 0:
                lr ^= 1
            triples.append((lr << 2) | 2 | ((value >> bit) & 1))
    return [sum(triples[i+j] << (21-3*j) for j in range(8)) for i in range(0,len(triples),8)]


class I2STests(unittest.TestCase):
    def test_i2s(self):
        result = validate_tone(decode_i2s(fixture()), 500)
        self.assertEqual(result["bits_per_slot"], 32)
        self.assertLess(result["maximum_pcm_error"], 2)

    def test_left_justified(self):
        result = validate_tone(decode_i2s(fixture("left"), "left"), 500)
        self.assertLess(result["maximum_pcm_error"], 2)

    def test_wrong_format(self):
        with self.assertRaises(ValueError):
            validate_tone(decode_i2s(fixture(), "left"), 500)

    def test_clock_low(self):
        words = fixture()
        words[0] &= ~(2 << 21)
        with self.assertRaisesRegex(ValueError, "BCLK low"):
            decode_i2s(words)

    def test_sine_corruption(self):
        decoded = decode_i2s(fixture())
        decoded[40] = (decoded[40][0], 32767)
        with self.assertRaisesRegex(ValueError, "requested sine"):
            validate_tone(decoded, 500)

    def test_recorded_gpio_capture(self):
        # Run against the real board trace when available; ordinary host
        # checkouts still run the protocol fixtures above.
        trace = Path(__file__).parents[1] / "build-tests/audio-validation/i2s-500.json"
        if not trace.exists():
            self.skipTest("No hardware trace available")
        result = validate_tone(decode_i2s(json.loads(trace.read_text())), 500)
        self.assertLess(result["maximum_pcm_error"], 2)


if __name__ == "__main__":
    unittest.main()
