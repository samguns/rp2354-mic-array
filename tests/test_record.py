import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("record", Path(__file__).parents[1] / "tools" / "record.py")
record = importlib.util.module_from_spec(spec)
spec.loader.exec_module(record)


class Port:
    def __init__(self, lines):
        self.lines = iter(lines)

    def readline(self):
        return next(self.lines)


class RecordTests(unittest.TestCase):
    def test_valid(self):
        rate, channels, pcm = record.read_pcm(Port([
            b"uart:~$ mic dump\r\n", b"PCM_BEGIN 15998 4 1\r\n",
            b"0100020003000400\r\n", b"PCM_END\r\n",
        ]))
        self.assertEqual((rate, channels, pcm), (15998, 4, bytes.fromhex("0100020003000400")))

    def test_truncated(self):
        with self.assertRaises(ValueError):
            record.read_pcm(Port([b"PCM_BEGIN 16000 4 1\n", b"0100\n", b"PCM_END\n"]))

    def test_extra_data(self):
        with self.assertRaises(ValueError):
            record.read_pcm(Port([b"PCM_BEGIN 16000 4 1\n", b"00" * 9 + b"\n"]))

    def test_corrupt(self):
        with self.assertRaises(ValueError):
            record.read_pcm(Port([b"PCM_BEGIN 16000 4 1\n", b"not hex\n"]))


if __name__ == "__main__":
    unittest.main()
