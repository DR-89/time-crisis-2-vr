"""Compile and exercise the production Frame ROM storage/importer classes."""
from pathlib import Path
import os
import sys
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def jdk_home():
    sys.path.insert(0, str(ROOT / "tools"))
    import host_paths
    return host_paths.java_home()

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
