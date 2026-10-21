from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tc2-link-') as temp:
    binary=Path(temp)/'fixture'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(ROOT/'tests/tc2_link_fixture.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
