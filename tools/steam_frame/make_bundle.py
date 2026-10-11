#!/usr/bin/env python3
"""Create a checksummed ROM-free Steam Frame installer bundle from a built APK."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent / "bundle"
DEFAULT_APK = ROOT / "artifacts" / "steam-frame" / "TimeCrisis2VR-SteamFrame-arm64.apk"
DEFAULT_INFO = ROOT / "artifacts" / "steam-frame" / "build-info.json"
DEFAULT_OUTPUT = ROOT / "artifacts" / "steam-frame" / "bundle"


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def create_bundle(apk, info, output):
    apk, info, output = Path(apk), Path(info), Path(output)
    metadata = json.loads(info.read_text(encoding="utf-8"))
    expected = {"package": "org.timecrisis2.frame", "abi": "arm64-v8a", "rom_bundled": False, "required_rom_chips": 22}
    if any(metadata.get(key) != value for key, value in expected.items()):
        raise ValueError("build-info.json must describe a ROM-free ARM64 Frame APK with 22 required chips")
    if digest(apk) != metadata.get("sha256"):
        raise ValueError("APK hash does not match build-info.json")
    if output.exists():
        shutil.rmtree(output)
    (output / "payload").mkdir(parents=True)
    shutil.copy2(apk, output / "payload" / "TimeCrisis2VR-SteamFrame-arm64.apk")
    shutil.copy2(info, output / "build-info.json")
    for name in ("install.py", "install.sh", "README.md"):
        shutil.copy2(SOURCE / name, output / name)
    rows = []
    for path in sorted(output.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            rows.append(digest(path) + "  " + str(path.relative_to(output)) + "\n")
    (output / "SHA256SUMS").write_text("".join(rows), encoding="ascii")
    print("Created ROM-free Frame bundle with " + str(len(rows)) + " checksummed files: " + str(output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apk", type=Path, default=DEFAULT_APK)
    parser.add_argument("--build-info", type=Path, default=DEFAULT_INFO)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    create_bundle(args.apk, args.build_info, args.output)


if __name__ == "__main__":
    main()
