"""Exercise the bounded Steam Frame per-eye resolution policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang") or shutil.which("cc")
if not compiler:
    raise SystemExit("clang or cc is required for the Frame eye-size fixture")
with tempfile.TemporaryDirectory(prefix="tc2-frame-dimensions-") as temp:
    executable = Path(temp) / "frame-dimensions"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-I", str(ROOT / "quest"),
                    str(ROOT / "tests/frame_dimensions_fixture.c"), "-lm", "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=5)
print("PASS: 2160 cap, aspect preservation, target clamps, runtime maximum and invalid dimensions")
