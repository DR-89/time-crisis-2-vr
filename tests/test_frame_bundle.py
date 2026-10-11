"""Exercise the shipped Frame bundle verifier and ROM dry-run with synthetic inputs."""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "steam_frame" / "make_bundle.py"
INSTALLER = ROOT / "tools" / "steam_frame" / "bundle" / "install.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("make_frame_bundle", GENERATOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rom_entries(count=22):
    chips = [(f"tc2_chip{index}.bin", f"test-chip-{index}".encode()) for index in range(count)]
    manifest = "".join(hashlib.sha256(data).hexdigest() + "  " + name + "\n" for name, data in chips)
    return chips, manifest


def write_apk(path, chips, manifest):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as apk:
        for name, data in (
            ("AndroidManifest.xml", b"manifest"), ("classes.dex", b"dex"),
            ("assets/roms.sha256", manifest.encode("ascii")),
            ("assets/models.sha256", b"model"),
            ("lib/arm64-v8a/libmain.so", b"main"),
            ("lib/arm64-v8a/libSDL2.so", b"sdl"),
            ("lib/arm64-v8a/libopenxr_loader.so", b"openxr"),
            ("assets/models/player-gun.tcgun", b"model"),
        ):
            apk.writestr(name, data)


def run(bundle, *args):
    return subprocess.run([sys.executable, str(bundle / "install.py"), *map(str, args)], text=True, capture_output=True)


def main():
    generator = load_generator()
    with tempfile.TemporaryDirectory(prefix="tc2-frame-bundle-") as temp:
        root = Path(temp)
        chips, manifest = rom_entries()
        apk = root / "synthetic.apk"
        write_apk(apk, chips, manifest)
        info = root / "build-info.json"
        receipt = {
            "apk": "synthetic.apk", "sha256": hashlib.sha256(apk.read_bytes()).hexdigest(),
            "package": "org.timecrisis2.frame", "abi": "arm64-v8a",
            "rom_bundled": False, "required_rom_chips": 22,
        }
        info.write_text(json.dumps(receipt), encoding="utf-8")
        bundle = root / "bundle"
        generator.create_bundle(apk, info, bundle)

        verified = run(bundle, "--verify-only")
        assert verified.returncode == 0, verified.stdout + verified.stderr
        assert "Bundle verified (5 files" in verified.stdout

        archive = root / "renamed-roms.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as rom_zip:
            for index, (_, data) in enumerate(chips):
                rom_zip.writestr(f"legacy/renamed-{index}.bin", data)
        dry_run = run(bundle, "--dry-run", "--rom", archive)
        assert dry_run.returncode == 0, dry_run.stdout + dry_run.stderr
        assert "ROM verified (22 required chips" in dry_run.stdout
        assert "Would back up" in dry_run.stdout

        short_archive = root / "missing-chip.zip"
        with zipfile.ZipFile(short_archive, "w", zipfile.ZIP_DEFLATED) as rom_zip:
            for index, (_, data) in enumerate(chips[:-1]):
                rom_zip.writestr(f"renamed-{index}.bin", data)
        rejected = run(bundle, "--dry-run", "--rom", short_archive)
        assert rejected.returncode != 0
        assert "ROM chips do not match" in rejected.stderr

        with (bundle / "README.md").open("a", encoding="utf-8") as text:
            text.write("tampered\n")
        tampered = run(bundle, "--verify-only")
        assert tampered.returncode != 0
        assert "checksum mismatch" in tampered.stderr
    print("PASS: synthetic 22-chip archive, renamed ZIP entries, verify-only, dry-run and tamper rejection")


if __name__ == "__main__":
    main()
