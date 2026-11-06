import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class DoaBufferTests(unittest.TestCase):
    def test_producer_consumer_ownership(self):
        if not shutil.which('gcc'):
            self.skipTest('Native GCC required')
        root=Path(__file__).parents[1]
        with tempfile.TemporaryDirectory() as folder:
            exe=Path(folder)/'buffers.exe'
            subprocess.run(['gcc','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),
                            str(root/'tests/doa_buffer_test.c'),'-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
