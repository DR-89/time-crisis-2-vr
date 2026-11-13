"""Test the actual online.cfg/server-switch code with isolated files and no network traffic."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = next((ROOT / '.tools/llvm-mingw').glob('*/bin/x86_64-w64-mingw32-clang.exe'))
with tempfile.TemporaryDirectory(prefix='tcvr-server-settings-') as temp:
    temp = Path(temp)
    shutil.copytree(ROOT / '.tools/sdl/SDL2-2.30.11/include', temp / 'SDL2')
    includes = [ROOT / p for p in ('quest', 'upstream/tc2/include', 'upstream/tc2/src', 'upstream/tc2/src/eng', 'upstream/tc2/gen')]
    exe = temp / 'server-settings.exe'
    subprocess.run([str(compiler), '-std=c11', '-O1', '-Wall', '-Wextra', '-Wno-unused-function',
                    '-Wno-unused-variable', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
                    '-DTCVR_PC', '-DSDL_MAIN_HANDLED', '-I' + str(temp), *['-I' + str(p) for p in includes],
                    str(ROOT / 'tests/server_settings_fixture.c'), '-lws2_32', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=temp, check=True, timeout=15)
