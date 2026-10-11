"""Verify the Frame SDL Java staging patch and null/present clipboard behavior."""
from pathlib import Path
import hashlib
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_frame
import patch_sdl_frame

TOOLS = Path(os.environ.get("TCVR_SHARED_TOOLS", ROOT / ".tools")).expanduser()
SDL_SOURCE = TOOLS / "sdl" / "SDL2-2.30.11" / "android-project" / "app" / "src" / "main" / "java"
SOURCE_HANDLER = SDL_SOURCE / patch_sdl_frame.HANDLER_PATH
ANDROID_JAR = build_frame.android_sdk() / "platforms" / "android-34" / "android.jar"
JAVA = build_frame.java_home() / "bin"


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


if not SOURCE_HANDLER.is_file():
    raise SystemExit(f"Pinned SDL 2.30.11 Java source not found: {SOURCE_HANDLER}; set TCVR_SHARED_TOOLS")
if not ANDROID_JAR.is_file():
    raise SystemExit(f"Android API 34 jar not found: {ANDROID_JAR}")

source_hash = file_sha256(SOURCE_HANDLER)
with tempfile.TemporaryDirectory(prefix="tc2-sdl-clipboard-") as temporary:
    temp = Path(temporary)
    generated = temp / "generated-sdl-java"
    metadata = patch_sdl_frame.prepare_frame_java_view(SDL_SOURCE, generated)
    patched_handler = generated / patch_sdl_frame.HANDLER_PATH
    patched_hash = file_sha256(patched_handler)
    assert metadata["patch_id"] == patch_sdl_frame.PATCH_ID
    assert metadata["source_sha256"] == source_hash
    assert metadata["patched_sha256"] == patched_hash
    assert metadata["generated_java_tree_sha256"] == patch_sdl_frame.java_tree_sha256(generated)
    assert patch_sdl_frame.PATCH_MARKER not in SOURCE_HANDLER.read_text(encoding="utf-8")
    assert file_sha256(SOURCE_HANDLER) == source_hash

    # A second application must be stable, while the pinned cache remains pristine.
    second = patch_sdl_frame.patch_handler_file(patched_handler)
    assert file_sha256(patched_handler) == patched_hash
    assert second["patched_sha256"] == patched_hash
    assert file_sha256(SOURCE_HANDLER) == source_hash

    # Exercise the patched production class with real Java and controllable Android clipboard stubs.
    fixture_source = temp / "fixture-src"
    fixture_classes = temp / "fixture-classes"
    fixture_source.mkdir()
    fixture_classes.mkdir()
    stubs = ROOT / "tests" / "sdl_clipboard_stubs"
    for stub in stubs.rglob("*.java"):
        destination = fixture_source / stub.relative_to(stubs)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(stub.read_bytes())
    source_text = patched_handler.read_text(encoding="utf-8")
    handler = source_text[source_text.index(patch_sdl_frame.PATCH_MARKER):]
    extracted = fixture_source / "org" / "libsdl" / "app" / "SDLClipboardHandler.java"
    extracted.write_text(
        "package org.libsdl.app;\n"
        "import android.content.ClipData;\n"
        "import android.content.ClipboardManager;\n"
        "import android.content.Context;\n" + handler,
        encoding="utf-8",
    )
    fixture_sources = sorted(fixture_source.rglob("*.java"))
    subprocess.run([str(JAVA / "javac"), "-source", "8", "-target", "8", "-d", str(fixture_classes),
                    *map(str, fixture_sources)], check=True)
    subprocess.run([str(JAVA / "java"), "-cp", str(fixture_classes), "org.libsdl.app.ClipboardProbe"],
                   check=True, timeout=10)

    # Compile the same generated SDL and app Java source set used by build_frame.package().
    production_classes = temp / "production-classes"
    production_classes.mkdir()
    production_sources = sorted(generated.rglob("*.java")) + sorted((ROOT / "quest" / "java").rglob("*.java"))
    argfile = temp / "javac.args"
    argfile.write_text("\n".join('"' + str(path).replace("\\", "/") + '"' for path in production_sources) + "\n",
                       encoding="utf-8")
    subprocess.run([str(JAVA / "javac"), "-source", "8", "-target", "8", "-classpath", str(ANDROID_JAR),
                    "-d", str(production_classes), "@" + str(argfile)], check=True)

print("PASS: pinned SDL source stays unchanged; Frame patch is stable; null and present clipboard paths pass; production Java compiles")
