"""The pause menu (quest/quest_menu.c) without a headset: builds tests/menu_fixture.c with the local LLVM/MinGW toolchain
(Build-PC.ps1 fetches it), which draws every tab, online state and dialog, checks that nothing leaves its area, and drives
the laser and stick controls. The pictures land in build/menu-test as PNGs over a dark backdrop, for a look."""
from pathlib import Path
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/menu-test'
OUT.mkdir(parents=True, exist_ok=True)
compiler = next((ROOT / '.tools/llvm-mingw').glob('*/bin/x86_64-w64-mingw32-clang.exe'))
exe = OUT / 'menu.exe'
subprocess.run([str(compiler), '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-missing-field-initializers',
                '-I' + str(ROOT / 'quest'), str(ROOT / 'tests/menu_fixture.c'), '-o', str(exe)], check=True)
run = subprocess.run([str(exe)], cwd=OUT, capture_output=True, text=True, timeout=120)
print(run.stdout[-400:], run.stderr[-2000:])
assert run.returncode == 0, 'menu fixture failed'
W, H = 1120, 728
for raw in sorted(OUT.glob('*.rgba')):
    panel = Image.frombytes('RGBA', (W, H), raw.read_bytes())
    back = Image.new('RGBA', (W + 80, H + 80), (16, 22, 30, 255))
    back.alpha_composite(panel, (40, 40))
    back.convert('RGB').save(raw.with_suffix('.png'))
    raw.unlink()
print('PASS: every tab, online state and dialog fits; laser, drag, repeat, sticks, tabs and B work.')
