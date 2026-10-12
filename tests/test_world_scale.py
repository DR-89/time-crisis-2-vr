"""Exercise production scene projection, aim and HUD depth without a headset."""
from pathlib import Path
import os, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = ROOT / '.tools/llvm-mingw/llvm-mingw-20260922-ucrt-x86_64/bin/x86_64-w64-mingw32-clang.exe'
with tempfile.TemporaryDirectory(prefix='tc2vr-scale-') as temp:
    exe = Path(temp) / 'scene.exe'
    subprocess.run([str(compiler), '-std=c11', '-O2', '-Wall',
                    '-I'+str(ROOT/'quest'), '-I'+str(ROOT/'upstream/tc2/src'),
                    str(ROOT/'tests/tc2_scene_fixture.c'), str(ROOT/'quest/tc2_scene.c'),
                    '-o', str(exe)], check=True)
    env = os.environ.copy()
    env.pop('TC2_UNITS_PER_METER', None)
    subprocess.run([str(exe)], cwd=temp, check=True, timeout=15, env=env)
