"""Exercise the real UDP heartbeat sender with deterministic timer scheduling."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = next((ROOT / '.tools/llvm-mingw').glob('*/bin/x86_64-w64-mingw32-clang.exe'))
with tempfile.TemporaryDirectory(prefix='tcvr-net-keepalive-') as tmp:
    tmp = Path(tmp)
    shutil.copytree(ROOT / '.tools/sdl/SDL2-2.30.11/include', tmp / 'SDL2')
    exe = tmp / 'net-keepalive.exe'
    subprocess.run([str(compiler), '-std=c11', '-O1', '-Wall', '-Wextra', '-DSDL_MAIN_HANDLED',
                    '-I' + str(tmp), '-I' + str(ROOT / 'quest'), '-I' + str(ROOT / 'upstream/tc2/src/eng'),
                    str(ROOT / 'tests/net_keepalive_fixture.c'), '-lws2_32', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
