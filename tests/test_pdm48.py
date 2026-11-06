import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pdm48_reference import coefficients, decode, response


class Pdm48Tests(unittest.TestCase):
    def test_quantized_coefficients_and_passband(self):
        for taps in coefficients():
            self.assertEqual(sum(taps), 16384)
            self.assertTrue(all(-32768 <= x <= 32767 for x in taps))
        for frequency in range(0, 20001, 100):
            self.assertLess(abs(20 * math.log10(response(frequency))), .15)

    def test_alias_images(self):
        for frequency in range(28000, 1536001, 1000):
            alias = min(frequency % 48000, 48000 - frequency % 48000)
            if alias <= 20000:
                self.assertLess(response(frequency), 10 ** (-60 / 20))

    def test_bitwise_reference_channel_isolation_and_gain(self):
        pcm = decode([0x95959595] * 4096)
        self.assertEqual(len(pcm), 512 * 4)
        for n in range(128, 512):
            self.assertEqual(pcm[n * 4:n * 4 + 4], [32767, -32768, 0, 0])


if __name__ == "__main__":
    unittest.main()
