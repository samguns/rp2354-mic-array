import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from verify_uac_pattern import verify


def pattern(values):
    return b''.join(struct.pack('<4H',v,v^0x1357,v^0x2468,v^0x55aa) for v in values)


class UacPatternTests(unittest.TestCase):
    def test_startup_and_wrap(self):
        result = verify(bytes(16*8)+pattern([65534,65535,0,1,2]))
        self.assertEqual(result['validated_frames'],5)
        self.assertEqual(result['startup_silence_frames'],16)

    def test_missing_duplicate_frame(self):
        for values in ([1,2,4],[1,2,2]):
            with self.assertRaises(ValueError):verify(pattern(values))

    def test_channel_swap(self):
        data = bytearray(pattern([1,2,3]));data[2:4],data[4:6] = data[4:6],data[2:4]
        with self.assertRaises(ValueError):verify(data)

    def test_silence_inside_stream(self):
        with self.assertRaises(ValueError):verify(pattern([1,2])+bytes(8)+pattern([3]))

    def test_no_pattern_or_truncation(self):
        for data in (bytes(128),b'1234'):
            with self.assertRaises(ValueError):verify(data)


if __name__=='__main__':unittest.main()
