"""Real socket session lifecycle and hostile/mismatched peer gate."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tc2-session-') as tmp:
    exe=Path(tmp)/'session'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(ROOT/'tests/tc2_session_fixture.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
