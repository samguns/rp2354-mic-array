import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

class TelemetryProtocolTests(unittest.TestCase):
    def test_strict_commands_and_integer_bounds(self):
        if not shutil.which('gcc'):
            self.skipTest('Native GCC required')
        root=Path(__file__).parents[1]
        with tempfile.TemporaryDirectory() as folder:
            exe=Path(folder)/'protocol.exe'
            subprocess.run(['gcc','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),
                            str(root/'tests/telemetry_protocol_test.c'),'-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
