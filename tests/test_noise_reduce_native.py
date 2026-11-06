import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class FullbandNativeTests(unittest.TestCase):
    def test_production_suppression_and_bandwidth(self):
        if not shutil.which('gcc'):
            self.skipTest('Native GCC needed for DSP verification')
        root=Path(__file__).parents[1]
        with tempfile.TemporaryDirectory() as folder:
            for source in ('noise_reduce_test.c','nr_fullband_test.c'):
                exe=Path(folder)/(source+'.exe')
                subprocess.run(['gcc','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),
                                str(root/'tests'/source),str(root/'src/noise_reduce.c'),'-lm','-o',str(exe)],check=True)
                result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
