"""Model asynchronous USB buffer ownership using the production TX/RX paths."""
import unittest

from native_support import GCC, ROOT, extract_functions, run_c_fixture


class UsbHandoffTests(unittest.TestCase):
    @unittest.skipUnless(GCC, 'Native GCC required')
    def test_metadata_is_visible_before_usb_ownership(self):
        driver = ROOT/'zephyrproject/zephyr/drivers/usb/udc/udc_rpi_pico.c'
        if not driver.exists():
            self.skipTest('Configured Zephyr workspace required')
        production = extract_functions(driver.read_text(encoding='utf-8'),
                                       'rpi_pico_prep_rx', 'rpi_pico_prep_tx')
        run_c_fixture('usb_handoff', production)


if __name__ == '__main__':
    unittest.main()
