"""Find the build computer's Android SDK, JDK and NDK host tools without user-specific paths.

The APK targets ARM64 Android, but Google ships the NDK's compilers only for macOS,
x86_64 Linux and Windows. This build supports macOS and x86_64 Linux; on Windows,
run it in WSL2 (Ubuntu), which is x86_64 Linux. The Frame itself is ARM64 and cannot
run the NDK.
"""
from pathlib import Path
import os
import platform
import shutil
import sys


def android_sdk():
    configured = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    if configured:
        return Path(configured).expanduser()
    mac, linux = Path.home() / "Library" / "Android" / "sdk", Path.home() / "Android" / "Sdk"
    candidates = (mac, linux) if sys.platform == "darwin" else (linux, mac)
    return next((path for path in candidates if path.is_dir()), candidates[0])


def _is_jdk(path):
    return (path / "bin" / "java").is_file() and (path / "bin" / "javac").is_file()


def java_home():
    configured = os.environ.get("JAVA_HOME")
    candidates = [Path(configured).expanduser()] if configured else []
    if sys.platform == "darwin":
        candidates += [
            Path("/Applications/Android Studio.app/Contents/jbr/Contents/Home"),
            Path("/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"),
            Path("/usr/local/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"),
        ]
    else:
        candidates += [Path("/usr/lib/jvm/java-17-openjdk-amd64"), Path("/usr/lib/jvm/java-17-openjdk"),
                       Path.home() / "android-studio" / "jbr"]
    for candidate in candidates:
        if _is_jdk(candidate):
            return candidate
    for command in ("javac", "java"):
        located = shutil.which(command)
        if located:
            candidate = Path(located).resolve().parent.parent
            if _is_jdk(candidate):
                return candidate
    raise RuntimeError("JDK 17 is required: set JAVA_HOME, or put java and javac on PATH")


def jni_platform_include():
    """The JDK's platform JNI header folder (jni_md.h) for this build computer."""
    return "darwin" if sys.platform == "darwin" else "linux"


def ndk_host():
    if sys.platform == "darwin":
        return "darwin-x86_64"   # (universal binaries; they run on Apple Silicon too)
    if sys.platform.startswith("linux") and platform.machine().lower() in ("x86_64", "amd64"):
        return "linux-x86_64"
    if sys.platform in ("win32", "cygwin"):
        raise RuntimeError("Build the Steam Frame / Quest APK in WSL2 (Ubuntu) on Windows; see README, Build from source")
    raise RuntimeError("Build on macOS or x86_64 Linux (or WSL2 on Windows): the Android NDK has no "
                       + platform.machine() + " Linux host tools, so the Frame itself can't build it")


def ndk_prebuilt(ndk):
    return Path(ndk) / "toolchains" / "llvm" / "prebuilt" / ndk_host()


def ndk_tool(ndk, name):
    return ndk_prebuilt(ndk) / "bin" / name
