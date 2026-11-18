"""Exercise the patched production cancellation code with mocked USB registers."""
import subprocess
import unittest

from native_support import GCC, ROOT, extract_functions, run_c_fixture


class UsbCancellationTests(unittest.TestCase):
    @unittest.skipUnless(GCC, 'Native GCC required')
    def test_pending_completions_and_atomic_disable(self):
        zephyr = ROOT/'zephyrproject/zephyr'
        driver = zephyr/'drivers/usb/udc/udc_rpi_pico.c'
        if not driver.exists():
            self.skipTest('Configured Zephyr workspace required')
        # Verify that the source under test includes the tracked patch.
        subprocess.run(['git', '-C', str(zephyr), 'apply', '--check', '--reverse',
                        str(ROOT/'patches/zephyr-udc-rpi-pico.patch')], check=True)
        production = extract_functions(driver.read_text(encoding='utf-8'),
                                       'udc_ep_to_bnum', 'get_ep_mask',
                                       'rpi_pico_ep_cancel', 'udc_rpi_pico_ep_disable')
        run_c_fixture('usb_cancel', production)


if __name__ == '__main__':
    unittest.main()
