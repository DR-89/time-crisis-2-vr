"""Compile the production Steam Frame OpenXR input path against a mock runtime."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = Path.home() / "Library" / "Android" / "sdk"
NDK = SDK / "ndk" / "27.2.12479018"
TOOLS = Path(os.environ.get("TCVR_SHARED_TOOLS", ROOT / ".tools")).expanduser()
SDL = TOOLS / "sdl" / "SDL2-2.30.11"
OPENXR = TOOLS / "openxr" / "prefab" / "modules" / "headers" / "include"
SYSROOT = NDK / "toolchains" / "llvm" / "prebuilt" / "darwin-x86_64" / "sysroot"
COMPILER = shutil.which("clang")
JDK = Path(os.environ.get("JAVA_HOME", "/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home")).expanduser()

if not COMPILER:
    raise SystemExit("clang is required for the production Frame input fixture")
if not (ROOT / "build/steam-frame/native/compat/SDL2/SDL.h").is_file():
    raise SystemExit("Run the Frame CMake configure step first to generate SDL compatibility headers")
if not (SYSROOT / "usr/include/jni.h").is_file() or not (OPENXR / "openxr/openxr.h").is_file():
    raise SystemExit("Pinned Android NDK or OpenXR headers are missing")

includes = [
    ROOT / "quest", ROOT / "quest/include", ROOT / "build/steam-frame/native/compat",
    ROOT / "upstream/tc2/src", ROOT / "upstream/tc2/src/eng", ROOT / "upstream/tc2/gen",
    ROOT / "upstream/tc2/include", SDL / "include", OPENXR,
    JDK / "include", JDK / "include/darwin",
]
with tempfile.TemporaryDirectory(prefix="tc2-frame-input-") as temp:
    executable = Path(temp) / "frame-input"
    command = [COMPILER, "-std=c11", "-O1", "-Wall", "-Wextra", "-Wno-unused-function",
               "-Wno-unused-variable", "-Wno-missing-field-initializers", "-DTCVR",
               "-DTCVR_STEAM_FRAME", "-DSDL_MAIN_HANDLED", "-DXR_USE_PLATFORM_ANDROID",
               "-DXR_USE_GRAPHICS_API_OPENGL_ES", "-Dgettid=getpid"]
    command += ["-I" + str(path) for path in includes]
    command += ["-idirafter", str(SYSROOT / "usr/include"), "-idirafter",
                str(SYSROOT / "usr/include/aarch64-linux-android")]
    command += [str(ROOT / "tests/handedness_fixture.c"), str(ROOT / "quest/quest_options.c"),
                str(ROOT / "quest/quest_cover.c"), "-lm", "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], cwd=temp, check=True, timeout=15)
