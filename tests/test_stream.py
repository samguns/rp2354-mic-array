import io
import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
from stream import HEADER, MAGIC, packets


def packet(seq, frames=128, dropped=0, faults=0, error=0, rate=16000):
    pcm = struct.pack("<4h", -32768, -1, 0, 32767) * frames
    return HEADER.pack(MAGIC, seq, rate, frames, dropped, faults, error,
                       zlib.crc32(pcm)) + pcm


class Fragmented(io.BytesIO):
    def read(self, count):
        return super().read(min(count, 7))


class StreamTests(unittest.TestCase):
    def test_fragmented_binary_and_terminal(self):
        data = b"mic stream 1\r\n" + packet(41) + packet(42) + packet(43, 0)
        result = list(packets(Fragmented(data)))
        self.assertEqual([seq for seq, _ in result], [41, 42])
        self.assertEqual(struct.unpack("<4h", result[0][1][:8]), (-32768,-1,0,32767))

    def test_48k_and_final_partial(self):
        data = packet(1,rate=48000) + packet(2,64,rate=48000) + packet(3,0,rate=48000)
        result = list(packets(io.BytesIO(data),48000))
        self.assertEqual(sum(len(pcm)//8 for _,pcm in result),192)

    def test_wrong_rate(self):
        with self.assertRaisesRegex(ValueError,"header"):
            list(packets(io.BytesIO(packet(1)),48000))

    def test_partial_middle(self):
        with self.assertRaisesRegex(ValueError,"partial"):
            list(packets(io.BytesIO(packet(1,64)+packet(2))))

    def test_oversized_packet(self):
        with self.assertRaisesRegex(ValueError,"header"):
            list(packets(io.BytesIO(packet(1,129))))

    def test_gap(self):
        with self.assertRaisesRegex(ValueError, "Missing"):
            list(packets(io.BytesIO(packet(1) + packet(3))))

    def test_corruption(self):
        data = bytearray(packet(1)); data[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            list(packets(io.BytesIO(data)))

    def test_truncation(self):
        with self.assertRaises(TimeoutError):
            list(packets(io.BytesIO(packet(1))))

    def test_terminal_error(self):
        with self.assertRaisesRegex(ValueError, "fault"):
            list(packets(io.BytesIO(packet(1) + packet(2, 0, error=1))))

    def test_capture_drop(self):
        with self.assertRaisesRegex(ValueError, "drop"):
            list(packets(io.BytesIO(packet(1) + packet(2, dropped=1))))

    def test_sequence_wrap(self):
        self.assertEqual(len(list(packets(io.BytesIO(packet(0xffffffff) + packet(0, 0))))), 1)


if __name__ == "__main__":
    unittest.main()
