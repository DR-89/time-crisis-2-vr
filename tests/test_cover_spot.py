"""Physical cover at the arcade's own slide, on the host: decoder, pedal, sign art."""
from pathlib import Path
import os, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tc2-cover-') as temp:
    binary = Path(temp) / 'cover'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g', '-DQSAFE_PAINT_ONLY',
                    '-I' + str(ROOT / 'quest'), '-I' + str(ROOT / 'upstream/include'),
                    str(ROOT / 'tests/cover_spot_fixture.c'), str(ROOT / 'quest/quest_cover.c'), str(ROOT / 'quest/quest_safe.c'),
                    '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=20, env=os.environ)
