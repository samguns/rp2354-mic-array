import array
import math
import sys
import tempfile
import unittest
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1]/"tools"))
from check_channels import analyze


class ChannelTests(unittest.TestCase):
    def check_levels(self, levels):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"tone.wav"
            samples = array.array("h", (round(level*math.sin(2*math.pi*1000*n/16000))
                                         for n in range(16000) for level in levels))
            if sys.byteorder != "little":
                samples.byteswap()
            with wave.open(str(path), "wb") as wav:
                wav.setparams((4,2,16000,0,"NONE","not compressed"))
                wav.writeframes(samples.tobytes())
            return analyze(path,"left")

    def test_exposed_left_is_channel_two(self):
        report = self.check_levels([100,400,100,100])
        self.assertEqual(report["strongest_channel"],2)
        self.assertEqual(report["assessment"],"consistent")

    def test_equal_levels_inconclusive(self):
        self.assertEqual(self.check_levels([100]*4)["assessment"],"inconclusive")


if __name__ == "__main__":
    unittest.main()
