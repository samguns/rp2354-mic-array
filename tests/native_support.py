"""Build native C fixtures containing functions extracted from production code."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GCC = shutil.which('gcc')


def extract_functions(source, *names):
    functions = []
    for name in names:
        start = source.index(name + '(')
        start = source.rfind('\n', 0, start) + 1
        brace = source.index('{', start)
        depth = 1
        end = brace + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        functions.append(source[start:end])
    return '\n\n'.join(functions) + '\n'


def run_c_fixture(name, production):
    fixture = (ROOT/'tests'/f'{name}_test.c').read_text(encoding='utf-8')
    marker = '/* PRODUCTION_FUNCTIONS */'
    if fixture.count(marker) != 1:
        raise ValueError(f'{name}: expected one production-function marker')
    with tempfile.TemporaryDirectory(prefix='usb-native-') as folder:
        source = Path(folder)/f'{name}.c'
        executable = Path(folder)/f'{name}.exe'
        source.write_text(fixture.replace(marker, production), encoding='utf-8')
        subprocess.run([GCC, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(source), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True,
                                text=True, timeout=10)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
