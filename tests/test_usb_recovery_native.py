"""Check USB recovery timing and progress detection without resetting capture."""
import unittest

from native_support import GCC, ROOT, extract_functions, run_c_fixture


class UsbRecoveryTests(unittest.TestCase):
    @unittest.skipUnless(GCC, 'Native GCC required')
    def test_active_stall_and_nonblocking_reconnect(self):
        source = (ROOT/'src/usb_audio.c').read_text(encoding='utf-8')
        run_c_fixture('usb_recovery', extract_functions(source, 'usb_audio_service'))


if __name__ == '__main__':
    unittest.main()
