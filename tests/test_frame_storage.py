"""Compile and exercise the production Frame ROM storage/importer classes."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def jdk_home():
    configured = os.environ.get("JAVA_HOME")
    candidates = [Path(configured).expanduser()] if configured else []
    candidates += [
        Path("/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"),
        Path("/usr/local/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"),
    ]
    for candidate in candidates:
        if (candidate / "bin/javac").is_file() and (candidate / "bin/java").is_file():
            return candidate
    for command in ("javac", "java"):
        located = shutil.which(command)
        if located:
            candidate = Path(located).resolve().parent.parent
            if (candidate / "bin/javac").is_file() and (candidate / "bin/java").is_file():
                return candidate
    raise SystemExit("JDK 17 is required for this production importer test")

java = jdk_home()
with tempfile.TemporaryDirectory(prefix="tc2-frame-storage-classes-") as directory:
    classes = Path(directory)
    subprocess.run([
        str(java / "bin/javac"), "-d", str(classes),
        str(ROOT / "quest/java/org/timecrisis/quest/RomInstaller.java"),
        str(ROOT / "quest/java/org/timecrisis2/frame/FrameStorage.java"),
        str(ROOT / "tests/FrameStorageTest.java"),
    ], check=True)
    subprocess.run([str(java / "bin/java"), "-cp", str(classes), "FrameStorageTest"], check=True)
