"""The arcade SOLO/LINKED shot that opens LAN Play: detector unit test on the host."""
from pathlib import Path
import subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tc2-coop-') as temp:
    binary = Path(temp) / 'coop'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
                    str(ROOT / 'tests/coop_choice_fixture.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
