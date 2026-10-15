"""Build the Steam Frame (default) or Quest ARM64 Android APK from local macOS toolchains."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET

import prepare_tc2
import patch_sdl_frame

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "steam-frame"
OUTPUT = ROOT / "artifacts" / "steam-frame" / "TimeCrisis2VR-SteamFrame-arm64.apk"
PACKAGE = "org.timecrisis2.frame"
APP_NAME = "TimeCrisis2VR"
SDL_SHA256 = "8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f"
OPENXR_SHA256 = "7e1b36141f9a4f1fa4a7e061936344fd9fcd36bce6c47eae2ad09812736167b6"
ANDROID_NS = "http://schemas.android.com/apk/res/android"
MANIFEST_SOURCE = ROOT / "quest" / "AndroidManifest-SteamFrame.xml"
KEYSTORE_NAME = "steam-frame-debug.keystore"
FLAVOR = "frame"
BUNDLE_ROMS = False
FLAVORS = {
    "frame": {"build": BUILD, "output": OUTPUT, "package": PACKAGE, "manifest": MANIFEST_SOURCE,
              "keystore": KEYSTORE_NAME},
    "quest": {"build": ROOT / "build" / "quest-mac",
              "output": ROOT / "artifacts" / "quest" / "TimeCrisis2VR-Quest-arm64.apk",
              "package": "org.timecrisis2.arcadevr", "manifest": ROOT / "quest" / "AndroidManifest.xml",
              "keystore": "quest-debug.keystore"},
}


def select_flavor(name, bundle_roms=False):
    """Quest shares the native host, lobby and link; only packaging differs."""
    global BUILD, OUTPUT, PACKAGE, MANIFEST_SOURCE, KEYSTORE_NAME, FLAVOR, BUNDLE_ROMS
    if bundle_roms and name != "quest":
        raise RuntimeError("--bundle-roms is only for private Quest builds; the Frame APK stays ROM-free")
    flavor = FLAVORS[name]
    BUILD, OUTPUT, PACKAGE = flavor["build"], flavor["output"], flavor["package"]
    MANIFEST_SOURCE, KEYSTORE_NAME = flavor["manifest"], flavor["keystore"]
    FLAVOR, BUNDLE_ROMS = name, bundle_roms
    if bundle_roms:
        OUTPUT = OUTPUT.with_name("TimeCrisis2VR-Quest-arm64-with-ROM.apk")


def run(command, cwd=ROOT, env=None):
    print("> " + " ".join(map(str, command)), flush=True)
    subprocess.run([str(x) for x in command], cwd=cwd, env=env, check=True)


def find_executable(name):
    configured = os.environ.get(name.upper())
    result = configured or shutil.which(name)
    if not result:
        raise RuntimeError(name + " is required on PATH")
    path = Path(result).expanduser()
    if not path.is_file():
        raise RuntimeError(name + " executable not found: " + str(path))
    return path


def android_sdk():
    configured = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    if configured:
        return Path(configured).expanduser()
    return Path.home() / "Library" / "Android" / "sdk"


def java_home():
    configured = os.environ.get("JAVA_HOME")
    candidates = [Path(configured).expanduser()] if configured else []
    candidates += [
        Path("/Applications/Android Studio.app/Contents/jbr/Contents/Home"),
        Path("/Applications/Android Studio Preview.app/Contents/jbr/Contents/Home"),
        Path("/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"),
        Path("/usr/local/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"),
    ]
    for candidate in candidates:
        if (candidate / "bin" / "java").is_file() and (candidate / "bin" / "javac").is_file():
            return candidate
    for command in ("javac", "java"):
        located = shutil.which(command)
        if located:
            candidate = Path(located).resolve().parent.parent
            if (candidate / "bin" / "java").is_file() and (candidate / "bin" / "javac").is_file():
                return candidate
    raise RuntimeError("JDK 17 is required; set JAVA_HOME or install Android Studio with its JBR")


def tools_root(argument):
    selected = argument or os.environ.get("TCVR_SHARED_TOOLS")
    return Path(selected).expanduser() if selected else ROOT / ".tools"


def verify_inputs(sdk, java, tools, ndk, build_tools):
    sdl = tools / "sdl" / "SDL2-2.30.11"
    xr = tools / "openxr" / "prefab" / "modules"
    required = [
        sdk / "platforms" / "android-34" / "android.jar",
        build_tools / "aapt2", build_tools / "zipalign", build_tools / "lib" / "d8.jar",
        build_tools / "lib" / "apksigner.jar", ndk / "build" / "cmake" / "android.toolchain.cmake",
        ndk / "toolchains" / "llvm" / "prebuilt" / "darwin-x86_64" / "bin" / "clang",
        ndk / "toolchains" / "llvm" / "prebuilt" / "darwin-x86_64" / "bin" / "llvm-strip",
        java / "bin" / "java", java / "bin" / "javac", java / "bin" / "jar", java / "bin" / "keytool",
        sdl / "CMakeLists.txt", sdl / "LICENSE.txt",
        xr / "openxr_loader" / "libs" / "android.arm64-v8a" / "libopenxr_loader.so",
        tools / "openxr" / "META-INF" / "LICENSE",
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError("Required Frame build inputs are missing:\n  " + "\n  ".join(missing))
    source = (sdl / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    version = {}
    for part in ("MAJOR", "MINOR", "MICRO"):
        match = re.search(r"(?m)^\s*set\(\s*SDL_" + part + r"_VERSION\s+(\d+)\s*\)\s*$", source)
        if match:
            version[part] = int(match.group(1))
    if version != {"MAJOR": 2, "MINOR": 30, "MICRO": 11}:
        raise RuntimeError("Expected pinned SDL 2.30.11 source at " + str(sdl))
    archives = {
        tools / "downloads" / "SDL2-2.30.11.tar.gz": SDL_SHA256,
        tools / "downloads" / "openxr_loader_for_android-1.1.43.aar": OPENXR_SHA256,
    }
    for archive, expected in archives.items():
        if not archive.is_file():
            raise RuntimeError("Pinned dependency archive is missing: " + str(archive))
        if sha256(archive) != expected:
            raise RuntimeError("Pinned dependency archive SHA-256 mismatch: " + str(archive))
    return sdl, xr


def prepare_native(args, sdk, java, tools, sdl, xr):
    if not args.rom.is_file():
        raise RuntimeError("ROM not found: " + str(args.rom))
    assets = prepare_tc2.prepare(args.rom, BUILD)
    ndk = sdk / "ndk" / "27.2.12479018"
    build_tools = sdk / "build-tools" / "35.0.0"
    cmake = find_executable("cmake")
    ninja = find_executable("ninja")
    env = os.environ.copy()
    env.update({"ANDROID_SDK_ROOT": str(sdk), "ANDROID_NDK_ROOT": str(ndk), "JAVA_HOME": str(java)})
    run([
        cmake, "-Wno-dev", "-S", ROOT / "quest", "-B", BUILD / "native", "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=" + str(ninja),
        "-DCMAKE_TOOLCHAIN_FILE=" + str(ndk / "build/cmake/android.toolchain.cmake"),
        "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-29", "-DANDROID_STL=c++_static",
        "-DCMAKE_BUILD_TYPE=Release", "-DTCVR_STEAM_FRAME=" + ("ON" if FLAVOR == "frame" else "OFF"),
        "-DSDL_ROOT=" + str(sdl),
        "-DXR=" + str(xr),
    ], env=env)
    run([cmake, "--build", BUILD / "native", "--parallel", args.jobs], env=env)
    upstream_revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT / "upstream", text=True).strip()
    source_revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    native_receipt = {
        "flavor": FLAVOR,
        "source_revision": source_revision,
        "upstream_revision": upstream_revision,
        "native_source_sha256": native_source_sha256(),
        **build_identity(sdk, java, tools, ndk, build_tools, cmake, ninja),
    }
    (BUILD / "native-inputs.json").write_text(json.dumps(native_receipt, indent=2) + "\n", encoding="utf-8")
    return assets


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def native_source_sha256():
    digest = hashlib.sha256()
    roots = [ROOT / "quest", ROOT / "link", ROOT / "tools" / "tc2_overlay.py", ROOT / "tools" / "tc2_reset_overlay.py", ROOT / "upstream" / "tc2" / "src",
             ROOT / "upstream" / "tc2" / "gen", ROOT / "upstream" / "tc2" / "include",
             ROOT / "tools" / "prepare_tc2.py", ROOT / "tools" / "patch_sdl_frame.py",
             ROOT / "tools" / "build_frame.py"]
    files = []
    for root in roots:
        if root.is_file():
            files.append(root)
        elif root.is_dir():
            files.extend(path for path in root.rglob("*") if path.is_file() and not path.is_symlink())
    for path in sorted(files):
        digest.update(str(path.relative_to(ROOT)).encode("utf-8") + b"\0")
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    return digest.hexdigest()


def version(command, *args):
    result = subprocess.run([str(command), *args], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=True)
    return result.stdout.strip().splitlines()[0]


def source_properties(path):
    properties = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            properties[key.strip()] = value.strip()
    return properties


def build_identity(sdk, java, tools, ndk, build_tools, cmake, ninja):
    openxr_lib = tools / "openxr" / "prefab" / "modules" / "openxr_loader" / "libs" / "android.arm64-v8a" / "libopenxr_loader.so"
    return {
        "toolchain": {
            "java": version(java / "bin" / "java", "-version"),
            "javac": version(java / "bin" / "javac", "-version"),
            "cmake": version(cmake, "--version"),
            "ninja": version(ninja, "--version"),
            "android_platform": source_properties(sdk / "platforms" / "android-34" / "source.properties"),
            "android_build_tools": source_properties(build_tools / "source.properties"),
            "android_ndk": source_properties(ndk / "source.properties"),
        },
        "dependencies": {
            "sdl": {"version": "2.30.11", "archive_sha256": sha256(tools / "downloads" / "SDL2-2.30.11.tar.gz")},
            "openxr_android_loader": {
                "version": "1.1.43",
                "archive_sha256": sha256(tools / "downloads" / "openxr_loader_for_android-1.1.43.aar"),
                "arm64_library_sha256": sha256(openxr_lib),
            },
        },
    }


def manifest_22(path):
    lines = path.read_text(encoding="ascii").splitlines()
    rows = []
    for line in lines:
        digest, sep, name = line.partition("  ")
        if not sep or len(digest) != 64 or any(ch not in "0123456789abcdef" for ch in digest):
            raise RuntimeError("Malformed ROM manifest row")
        if name != "c71.bin":
            rows.append((digest, name))
    if len(rows) != 22 or len({name for _, name in rows}) != 22 or len({digest for digest, _ in rows}) != 22:
        raise RuntimeError("Expected 22 distinct Time Crisis II chip hashes after excluding optional c71.bin")
    return rows


def write_frame_manifest(source, destination, rom_free_quest=False):
    ET.register_namespace("android", ANDROID_NS)
    tree = ET.parse(source)
    if rom_free_quest:
        # As tools/build.py: the 2D setup activity launches and imports the ROM,
        # then hands off to the VR-marked activity.
        name = "{" + ANDROID_NS + "}name"
        activities = {a.get(name): a for a in tree.getroot().find("application").findall("activity")}
        main, setup = activities[".MainActivity"], activities[".LauncherActivity"]
        for intent in main.findall("intent-filter"):
            for category in list(intent.findall("category")):
                if category.get(name) == "android.intent.category.LAUNCHER":
                    intent.remove(category)
        intent = ET.SubElement(setup, "intent-filter")
        ET.SubElement(intent, "action", {name: "android.intent.action.MAIN"})
        ET.SubElement(intent, "category", {name: "android.intent.category.LAUNCHER"})
    destination.parent.mkdir(parents=True, exist_ok=True)
    tree.write(destination, encoding="utf-8", xml_declaration=True)
    return tree


def manifest_activity_name(activity, package):
    name = activity.get("{" + ANDROID_NS + "}name")
    if not name:
        raise RuntimeError("Frame source manifest contains an activity without android:name")
    if name.startswith("."):
        return package + name
    if "." not in name:
        return package + "." + name
    return name


def validate_frame_apk_manifest(apk, aapt2, source_manifest):
    source_root = ET.parse(source_manifest).getroot()
    package = source_root.get("package")
    android_attr = lambda element, name: element.get("{" + ANDROID_NS + "}" + name)
    version_code = android_attr(source_root, "versionCode")
    version_name = android_attr(source_root, "versionName")
    uses_sdk = source_root.find("uses-sdk")
    application = source_root.find("application")
    if not package or not version_code or not version_name or uses_sdk is None or application is None:
        raise RuntimeError("Frame source manifest is missing package, version, SDK, or application metadata")
    min_sdk = android_attr(uses_sdk, "minSdkVersion")
    target_sdk = android_attr(uses_sdk, "targetSdkVersion")
    if not min_sdk or not target_sdk:
        raise RuntimeError("Frame source manifest is missing minimum or target SDK metadata")

    def capture(command):
        print("> " + " ".join(map(str, command)), flush=True)
        result = subprocess.run([str(part) for part in command], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
        return result.stdout

    xmltree = capture([aapt2, "dump", "xmltree", apk, "--file", "AndroidManifest.xml"])
    if "N: android=" + ANDROID_NS not in xmltree:
        raise RuntimeError("Compiled Frame APK manifest lost the canonical android namespace prefix")
    for activity in application.findall("activity"):
        name = manifest_activity_name(activity, package)
        # aapt2 keeps the manifest's own spelling; the Quest manifest uses relative names.
        if not any('"' + spelling + '"' in xmltree for spelling in (name, android_attr(activity, "name"))):
            raise RuntimeError("Compiled Frame APK is missing manifest activity " + name)

    badging = capture([aapt2, "dump", "badging", apk])
    launchable = None
    for activity in application.findall("activity"):
        for intent_filter in activity.findall("intent-filter"):
            actions = {node.get("{" + ANDROID_NS + "}name") for node in intent_filter.findall("action")}
            categories = {node.get("{" + ANDROID_NS + "}name") for node in intent_filter.findall("category")}
            if "android.intent.action.MAIN" in actions and "android.intent.category.LAUNCHER" in categories:
                launchable = manifest_activity_name(activity, package)
    expected = [
        f"package: name='{package}' versionCode='{version_code}' versionName='{version_name}'",
        f"minSdkVersion:'{min_sdk}'", f"targetSdkVersion:'{target_sdk}'",
    ]
    if launchable:
        expected.append(f"launchable-activity: name='{launchable}'")
    if not all(any(line.startswith(prefix) for line in badging.splitlines()) for prefix in expected):
        raise RuntimeError("Compiled Frame APK package, version, SDK, or launcher metadata changed")
    if not launchable and any(line.startswith("launchable-activity:") for line in badging.splitlines()):
        raise RuntimeError("Compiled Frame APK unexpectedly declares a launcher activity")


def package(assets, sdk, java, tools):
    build_tools = sdk / "build-tools" / "35.0.0"
    android_jar = sdk / "platforms" / "android-34" / "android.jar"
    ndk = sdk / "ndk" / "27.2.12479018"
    strip = ndk / "toolchains" / "llvm" / "prebuilt" / "darwin-x86_64" / "bin" / "llvm-strip"
    sdl = tools / "sdl" / "SDL2-2.30.11"
    xr_root = tools / "openxr"
    xr = xr_root / "prefab" / "modules"
    manifest_path = BUILD / "assets" / "roms.sha256"
    if not assets.is_dir() or not manifest_path.is_file():
        raise RuntimeError("Validated build/assets are required; run a normal Frame build with --rom first")
    rows = manifest_22(manifest_path)
    if not (BUILD / "native" / "libmain.so").is_file() or not (BUILD / "native" / "sdl" / "libSDL2.so").is_file():
        raise RuntimeError("Frame native libraries are missing; run a normal Frame build first")
    native_receipt_path = BUILD / "native-inputs.json"
    if not native_receipt_path.is_file():
        raise RuntimeError("Native source receipt is missing; run a normal Frame build first")
    native_receipt = json.loads(native_receipt_path.read_text(encoding="utf-8"))
    current_source_sha = native_source_sha256()
    if native_receipt.get("flavor", "frame") != FLAVOR:
        raise RuntimeError("Native libraries were built for another flavor; run a normal build")
    if native_receipt.get("native_source_sha256") != current_source_sha:
        raise RuntimeError("Native inputs changed since compilation; run a normal Frame build instead of package-only")

    classes = BUILD / "classes"
    dex = BUILD / "dex"
    if classes.exists(): shutil.rmtree(classes)
    if dex.exists(): shutil.rmtree(dex)
    classes.mkdir(parents=True)
    dex.mkdir(parents=True)
    java_source_root = tools / "sdl" / "SDL2-2.30.11" / "android-project" / "app" / "src" / "main" / "java"
    java_root = BUILD / "generated-sdl-java"
    sdl_java_metadata = patch_sdl_frame.prepare_frame_java_view(java_source_root, java_root)
    sources = sorted(java_root.rglob("*.java")) + sorted((ROOT / "quest" / "java").rglob("*.java"))
    argfile = BUILD / "javac.args"
    argfile.write_text("\n".join('"' + str(p).replace('\\', '/') + '"' for p in sources) + "\n", encoding="utf-8")
    run([java / "bin" / "javac", "-source", "8", "-target", "8", "-classpath", android_jar, "-d", classes, "@" + str(argfile)])
    jar = BUILD / "classes.jar"
    run([java / "bin" / "jar", "--create", "--file", jar, "-C", classes, "."])
    run([java / "bin" / "java", "-cp", build_tools / "lib" / "d8.jar", "com.android.tools.r8.D8", "--lib", android_jar, "--min-api", "29", "--output", dex, jar])

    source_models = sorted((ROOT / "quest" / "assets" / "models").glob("*.tcgun"))
    if not source_models or not any(path.name == "player-gun.tcgun" for path in source_models):
        raise RuntimeError("Required TC2 weapon model is missing")
    BUILD.mkdir(parents=True, exist_ok=True)
    model_manifest = "".join(sha256(model) + "  " + model.name + "\n" for model in source_models)
    apk_base = BUILD / "unaligned.apk"
    public_manifest = BUILD / "roms.sha256"
    public_manifest.write_text("".join(digest + "  " + name + "\n" for digest, name in rows), encoding="ascii")
    manifest_source = MANIFEST_SOURCE
    manifest_out = BUILD / manifest_source.name
    manifest_tree = write_frame_manifest(manifest_source, manifest_out,
                                         rom_free_quest=FLAVOR == "quest" and not BUNDLE_ROMS)
    manifest_package = manifest_tree.getroot().get("package")
    if manifest_package != PACKAGE:
        raise RuntimeError("Manifest package ID changed unexpectedly: " + str(manifest_package))

    licenses = BUILD / "public-assets" / "licenses"
    public = BUILD / "public-assets"
    if public.exists(): shutil.rmtree(public)
    licenses.mkdir(parents=True)
    license_sources = [
        (ROOT / "LICENSE", "TimeCrisis2VR-LICENSE.txt"),
        (ROOT / "NOTICE.md", "NOTICE.md"),
        (ROOT / "quest" / "vendor" / "openvr" / "LICENSE", "OpenVR-LICENSE.txt"),
        (ROOT / "upstream" / "LICENSE", "namco22-LICENSE.txt"),
        (sdl / "LICENSE.txt", "SDL2-LICENSE.txt"),
        (xr_root / "META-INF" / "LICENSE", "OpenXR-LICENSE.txt"),
    ]
    for source, name in license_sources:
        shutil.copy2(source, licenses / name)
    (public / "models").mkdir()
    for model in source_models:
        shutil.copy2(model, public / "models" / model.name)
    (public / "models.sha256").write_text(model_manifest, encoding="ascii")
    shutil.copy2(public_manifest, public / "roms.sha256")
    if (public / "roms").exists() or any(path.suffix.lower() == ".zip" for path in public.rglob("*")):
        raise RuntimeError("ROM payload entered the public APK staging directory")
    if BUNDLE_ROMS:
        # Private sideload only: the user's own verified chips, never published.
        (public / "roms").mkdir()
        for _, name in rows:
            shutil.copy2(assets / "roms" / name, public / "roms" / name)
    if apk_base.exists(): apk_base.unlink()
    run([build_tools / "aapt2", "link", "--manifest", manifest_out, "-I", android_jar, "-A", public, "-o", apk_base])

    package_libs = BUILD / "package-libs"
    if package_libs.exists(): shutil.rmtree(package_libs)
    package_libs.mkdir()
    libraries = [
        (BUILD / "native" / "libmain.so", package_libs / "libmain.so"),
        (BUILD / "native" / "sdl" / "libSDL2.so", package_libs / "libSDL2.so"),
        (xr / "openxr_loader" / "libs" / "android.arm64-v8a" / "libopenxr_loader.so", package_libs / "libopenxr_loader.so"),
    ]
    for source, destination in libraries:
        run([strip, "--strip-unneeded", "-o", destination, source])
    with zipfile.ZipFile(apk_base, "a", zipfile.ZIP_DEFLATED) as apk:
        names = set(apk.namelist())
        if not BUNDLE_ROMS and any(name.startswith("assets/roms/") or name.endswith("timecrs2.zip") for name in names):
            raise RuntimeError("A ROM file was staged in the ROM-free APK")
        for entry in sorted(dex.glob("*.dex")):
            apk.write(entry, entry.name)
        for _, library in libraries:
            apk.write(library, "lib/arm64-v8a/" + library.name)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    run([build_tools / "zipalign", "-f", "-P", "16", "4", apk_base, OUTPUT])

    key = Path.home() / ".config" / "timecrisis2vr" / KEYSTORE_NAME
    key.parent.mkdir(parents=True, exist_ok=True)
    if not key.exists():
        run([java / "bin" / "keytool", "-genkeypair", "-keystore", key, "-storepass", "android", "-alias", "androiddebugkey", "-keypass", "android", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-dname", "CN=Time Crisis II VR Frame,O=Android,C=US"])
    signer = [java / "bin" / "java", "-jar", build_tools / "lib" / "apksigner.jar"]
    run(signer + ["sign", "--ks", key, "--ks-key-alias", "androiddebugkey", "--ks-pass", "pass:android", "--key-pass", "pass:android", "--v1-signing-enabled", "false", "--v2-signing-enabled", "true", "--v3-signing-enabled", "false", OUTPUT])
    run(signer + ["verify", "--verbose", OUTPUT])
    run([build_tools / "zipalign", "-c", "-P", "16", "4", OUTPUT])
    validate_frame_apk_manifest(OUTPUT, build_tools / "aapt2", manifest_out)

    submodule = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT / "upstream", text=True).strip()
    root_revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=all"], cwd=ROOT, text=True).strip())
    version_name = manifest_tree.getroot().get("{http://schemas.android.com/apk/res/android}versionName")
    version_code = manifest_tree.getroot().get("{http://schemas.android.com/apk/res/android}versionCode")
    report = {
        "apk": OUTPUT.name, "sha256": sha256(OUTPUT), "bytes": OUTPUT.stat().st_size,
        "source_revision": root_revision, "upstream_revision": submodule,
        "native_source_sha256": current_source_sha, "source_tree_dirty": dirty,
        "flavor": FLAVOR, "package": PACKAGE, "abi": "arm64-v8a", "version_name": version_name,
        "version_code": int(version_code), "rom_bundled": BUNDLE_ROMS, "required_rom_chips": 22,
        "rom_manifest_sha256": sha256(public_manifest), "hardware_tested": False,
        "toolchain": native_receipt["toolchain"], "dependencies": native_receipt["dependencies"],
        "sdl_frame_java": {
            **sdl_java_metadata,
            "generated_source_view": str(java_root.relative_to(ROOT)),
            "patch_script_sha256": sha256(ROOT / "tools" / "patch_sdl_frame.py"),
        },
    }
    (OUTPUT.parent / "build-info.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=Path.home() / "Downloads" / "timecrs2.zip", help="user-supplied Time Crisis II US TSS3 Ver.B ZIP")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--package-only", action="store_true", help="package already-built native code and prepared assets")
    parser.add_argument("--shared-tools", type=Path, help="reuse a verified local .tools cache without copying it")
    parser.add_argument("--flavor", choices=sorted(FLAVORS), default="frame", help="frame (default) or quest")
    parser.add_argument("--bundle-roms", action="store_true", help="Quest only: private APK containing your own ROM chips")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("The initial Frame build flavor currently targets macOS hosts")
    try:
        select_flavor(args.flavor, args.bundle_roms)
        sdk = android_sdk()
        java = java_home()
        tools = tools_root(args.shared_tools)
        ndk = sdk / "ndk" / "27.2.12479018"
        build_tools = sdk / "build-tools" / "35.0.0"
        sdl, xr = verify_inputs(sdk, java, tools, ndk, build_tools)
        if args.package_only:
            package(BUILD / "assets", sdk, java, tools)
        else:
            assets = prepare_native(args, sdk, java, tools, sdl, xr)
            package(assets, sdk, java, tools)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
