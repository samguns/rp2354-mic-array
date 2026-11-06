import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from doa_check import command,parse_onboard


class DoaProtocolTests(unittest.TestCase):
    def test_waits_for_complete_result_after_old_prompt(self):
        class Port:
            def __init__(self):
                self.chunks=iter([b'uart:~$ ',b'DOA_RESULT status=rejected\r\n',
                                  b'DOA_REJECT bits: 1=low_level\r\n',b'uart:~$ '])
            def write(self,data):
                self.written=data
            def read(self,size):
                return next(self.chunks)
        port=Port();text=command(port,'doa stats')
        self.assertIn('DOA_REJECT bits:',text)
        self.assertEqual(port.written,b'doa stats\r')

    def test_incomplete_result_rejected(self):
        with self.assertRaisesRegex(ValueError,'Missing onboard'):
            parse_onboard('DOA_RESULT status=rejected angle_mdeg=-1 reject_mask=1')
