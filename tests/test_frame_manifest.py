"""Exercise Steam Frame manifest serialization through the pinned Android linker."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_frame

SDK = Path(os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME") or
           Path.home() / "Library" / "Android" / "sdk").expanduser()
BUILD_TOOLS = SDK / "build-tools" / "35.0.0"
AAPT2 = BUILD_TOOLS / "aapt2"
ANDROID_JAR = SDK / "platforms" / "android-34" / "android.jar"
SOURCE_MANIFEST = ROOT / "quest" / "AndroidManifest-SteamFrame.xml"

if not AAPT2.is_file() or not ANDROID_JAR.is_file():
    raise SystemExit("Android SDK build-tools 35.0.0 and platform android-34 are required for this regression")

with tempfile.TemporaryDirectory(prefix="tc2-frame-manifest-") as temp:
    directory = Path(temp)
    assets = directory / "assets"
    assets.mkdir()
    # Reproduce ElementTree's previous namespace rewriting as a negative control.
    rewritten_manifest = directory / "rewritten.xml"
    ET.parse(SOURCE_MANIFEST).write(rewritten_manifest, encoding="utf-8", xml_declaration=True)
    assert "xmlns:ns0=" in rewritten_manifest.read_text(encoding="utf-8")

    canonical_manifest = directory / "canonical.xml"
    build_frame.write_frame_manifest(SOURCE_MANIFEST, canonical_manifest)

    canonical_apk = directory / "canonical.apk"
    rewritten_apk = directory / "rewritten.apk"
    for manifest, apk in ((canonical_manifest, canonical_apk), (rewritten_manifest, rewritten_apk)):
        subprocess.run([str(AAPT2), "link", "--manifest", str(manifest), "-I", str(ANDROID_JAR),
                        "-A", str(assets), "-o", str(apk)], check=True, capture_output=True, text=True)

    # AAPT accepts both forms and reports identical metadata; the prefix guard catches the Lepton-sensitive form.
    canonical_badging = subprocess.run([str(AAPT2), "dump", "badging", str(canonical_apk)],
                                       check=True, capture_output=True, text=True).stdout
    rewritten_badging = subprocess.run([str(AAPT2), "dump", "badging", str(rewritten_apk)],
                                       check=True, capture_output=True, text=True).stdout
    assert canonical_badging == rewritten_badging
    build_frame.validate_frame_apk_manifest(canonical_apk, AAPT2, SOURCE_MANIFEST)
    try:
        build_frame.validate_frame_apk_manifest(rewritten_apk, AAPT2, SOURCE_MANIFEST)
    except RuntimeError as error:
        assert "canonical android namespace prefix" in str(error)
    else:
        raise AssertionError("The compiled ns0 manifest was not rejected")

print("PASS: real aapt2 preserves package/version/SDK/activities; ns0 namespace regression is rejected")
