#!/usr/bin/env python3
"""Verify or install a ROM-free Time Crisis II VR Steam Frame bundle."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
import zipfile

TITLE = "TimeCrisis2VR"
APK_NAME = "TimeCrisis2VR-SteamFrame-arm64.apk"
BASE = Path(__file__).resolve().parent
APK = BASE / "payload" / APK_NAME


def fail(message):
    raise SystemExit("TimeCrisis2VR Frame installer: " + message)


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def verify_manifest(text):
    rows = []
    for line in text.splitlines():
        if not line:
            continue
        checksum, sep, name = line.partition("  ")
        if not sep or not re.fullmatch(r"[0-9a-f]{64}", checksum):
            fail("APK has a malformed 22-chip ROM manifest")
        rows.append((checksum, name))
    if len(rows) != 22 or len({name for _, name in rows}) != 22 or len({sha for sha, _ in rows}) != 22:
        fail("APK manifest must contain exactly 22 distinct ROM chips")
    return {sha for sha, _ in rows}


def verify_apk(path=APK):
    try:
        with zipfile.ZipFile(path) as apk:
            if apk.testzip():
                fail("APK ZIP integrity check failed")
            names = set(apk.namelist())
            forbidden = [name for name in names if name.startswith("assets/roms/") or name.lower().endswith("timecrs2.zip")]
            if forbidden:
                fail("ROM payload is present in the APK")
            for name in ("AndroidManifest.xml", "classes.dex", "assets/roms.sha256",
                         "assets/models.sha256", "lib/arm64-v8a/libmain.so",
                         "lib/arm64-v8a/libSDL2.so", "lib/arm64-v8a/libopenxr_loader.so"):
                if name not in names:
                    fail("APK is missing " + name)
            required = verify_manifest(apk.read("assets/roms.sha256").decode("ascii"))
    except (OSError, zipfile.BadZipFile, KeyError, UnicodeError) as error:
        fail("cannot inspect APK: " + str(error))
    return required


def verify_bundle():
    checksums = BASE / "SHA256SUMS"
    if not checksums.is_file():
        fail("SHA256SUMS is missing")
    entries = []
    for line in checksums.read_text(encoding="ascii").splitlines():
        if not line:
            continue
        checksum, sep, name = line.partition("  ")
        relative = Path(name)
        if not sep or not re.fullmatch(r"[0-9a-f]{64}", checksum):
            fail("invalid checksum entry")
        if relative.is_absolute() or ".." in relative.parts:
            fail("unsafe checksum path")
        path = BASE / relative
        if path.is_symlink() or not path.is_file():
            fail("missing or symlinked file: " + name)
        if digest(path) != checksum:
            fail("checksum mismatch: " + name)
        entries.append(name)
    required_files = {"install.py", "install.sh", "README.md", "build-info.json", "payload/" + APK_NAME}
    if len(entries) != len(set(entries)):
        fail("duplicate checksum entries")
    if set(entries) != required_files:
        fail("bundle files do not match the expected ROM-free package")
    info = json.loads((BASE / "build-info.json").read_text(encoding="utf-8"))
    if (info.get("package") != "org.timecrisis2.frame" or info.get("abi") != "arm64-v8a" or
            info.get("rom_bundled") is not False or info.get("required_rom_chips") != 22):
        fail("build receipt does not describe the expected Steam Frame APK")
    verify_apk()
    print("Bundle verified (5 files; APK is ARM64 and contains no game ROM files).")


def verify_rom(path, required):
    try:
        with zipfile.ZipFile(path) as archive:
            if archive.testzip():
                fail("ROM archive has a CRC error")
            supplied = {
                hashlib.sha256(archive.read(info)).hexdigest()
                for info in archive.infolist() if not info.is_dir()
            }
        if required - supplied:
            fail("ROM chips do not match this Time Crisis II build")
    except (OSError, zipfile.BadZipFile) as error:
        fail("cannot verify ROM archive: " + str(error))
    print("ROM verified (22 required chips; ZIP filenames are ignored).")


def run(argv, *, capture=False):
    result = subprocess.run(argv, text=True, capture_output=capture)
    if result.returncode:
        fail("command failed: " + shlex.join(str(part) for part in argv))
    return result.stdout if capture else None


def registration_response(reply):
    try:
        response = json.loads(reply)
    except json.JSONDecodeError:
        fail("Steam registration returned unreadable output: " + reply.strip())
    if not isinstance(response, dict) or "error" in response or not isinstance(response.get("success"), str):
        fail("Steam title registration failed: " + str(response))
    return response["success"]


def backup_verification_script(backup):
    return '''python3 - "$HOME/Documents/TimeCrisis2VR" "$HOME/'''+backup+'''/data" "$HOME/devkit-game/TimeCrisis2VR" "$HOME/'''+backup+'''/title" "$HOME/'''+backup+'''" <<'PY'
import hashlib
import os
import stat
import sys

def inventory(root):
    root = os.path.abspath(root)
    entries = []
    for current, dirs, files in os.walk(root, topdown=True, followlinks=False):
        names = list(dirs) + list(files)
        for name in names:
            path = os.path.join(current, name)
            relative = os.path.relpath(path, root)
            info = os.lstat(path)
            mode = stat.S_IMODE(info.st_mode)
            if stat.S_ISLNK(info.st_mode):
                entries.append((relative, "link", mode, info.st_uid, info.st_gid, os.readlink(path)))
                if name in dirs:
                    dirs.remove(name)
            elif stat.S_ISDIR(info.st_mode):
                entries.append((relative, "dir", mode, info.st_uid, info.st_gid, ""))
            elif stat.S_ISREG(info.st_mode):
                digest = hashlib.sha256()
                with open(path, "rb") as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(block)
                entries.append((relative, "file", mode, info.st_uid, info.st_gid, digest.hexdigest()))
            else:
                raise SystemExit("unsupported file type in backup: " + path)
    return sorted(entries)

data_src, data_copy, title_src, title_copy, backup_root = sys.argv[1:]
for source, copy, absent in ((data_src, data_copy, "data-absent"),
                             (title_src, title_copy, "title-absent")):
    if os.path.isdir(source):
        if inventory(source) != inventory(copy):
            raise SystemExit("backup content or permissions differ: " + source)
    elif not os.path.isfile(os.path.join(backup_root, absent)):
        raise SystemExit("missing first-run backup marker: " + absent)
print("Pre-install backups verified: bytes, file types, ownership and permissions.")
PY'''


def install(args):
    if args.host and (not re.fullmatch(r"[A-Za-z0-9_.@:-]+", args.host) or args.host.startswith("-")):
        fail("invalid SSH host")
    if args.host_key_alias and (not re.fullmatch(r"[A-Za-z0-9_.:-]+", args.host_key_alias) or args.host_key_alias.startswith("-")):
        fail("invalid host key alias")
    if args.rom is None or not args.rom.is_file():
        fail("supply your own ROM archive with --rom")
    required = verify_apk()
    verify_rom(args.rom, required)
    if args.dry_run:
        print("Would back up the existing title and Documents/TimeCrisis2VR, copy the APK and your ROM archive separately, and register TimeCrisis2VR on " + (args.host or "your Frame") + ".")
        return
    if not args.install:
        fail("add --install to write to a Frame; --dry-run performs local validation only")
    if not args.host:
        fail("set --host steamos@<your Frame's address> (or FRAME_HOST)")
    # Optional: a script that books a shared Frame (claim/release), so another
    # tool cannot launch something mid-install.
    lock = Path(args.lock_script).expanduser() if args.lock_script else None
    if lock is not None and not lock.is_file():
        fail("Frame booking script not found: " + str(lock))
    if args.identity and not Path(args.identity).expanduser().is_file():
        fail("SSH identity not found: " + args.identity)
    ssh = ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=10"]
    scp = ["scp", "-q", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes"]
    if args.host_key_alias:
        ssh += ["-o", "HostKeyAlias=" + args.host_key_alias]
        scp += ["-o", "HostKeyAlias=" + args.host_key_alias]
    if args.identity:
        # The SteamOS Devkit Client's key, for example; other keys are not offered.
        identity = ["-o", "IdentitiesOnly=yes", "-i", str(Path(args.identity).expanduser())]
        ssh += identity
        scp += identity
    ssh += [args.host]
    scp_host = args.host
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    holder = "timecrisis2-frame-install"
    remote = lambda command, capture=False: run(ssh + [command], capture=capture)
    if lock is not None:
        run([str(lock), "claim", holder, "Time Crisis II VR verified local bundle install", "10"])
    try:
        busy = remote("podman ps --format '{{.Names}}'", capture=True)
        if any("lepton-steamlaunch-" in name or name == "lepton-dev" for name in busy.splitlines()):
            fail("a Lepton game/container is active; close it before installation")
        running = remote("ps -eo args | grep -E 'devkit-game/|OpenXR|openxr' | grep -v -E 'grep|vrserver|vrcompositor|vrmonitor' || true", capture=True)
        if running.strip():
            fail("a scene application may be active; close it before installation")
        backup = "Documents/TimeCrisis2VR-backups/" + stamp
        remote('''set -eu
backup="$HOME/''' + backup + '''"
for path in "$HOME/Documents/TimeCrisis2VR" "$HOME/devkit-game/TimeCrisis2VR"; do
  test ! -L "$path" || { echo "Refusing symlinked install path: $path" >&2; exit 1; }
done
mkdir -p "$backup"
if test -d "$HOME/Documents/TimeCrisis2VR"; then cp -a "$HOME/Documents/TimeCrisis2VR" "$backup/data"; else touch "$backup/data-absent"; fi
if test -d "$HOME/devkit-game/TimeCrisis2VR"; then cp -a "$HOME/devkit-game/TimeCrisis2VR" "$backup/title"; else touch "$backup/title-absent"; fi
test -f "$backup/data-absent" || test -d "$backup/data"
test -f "$backup/title-absent" || test -d "$backup/title"
''')
        remote(backup_verification_script(backup))
        remote('''set -eu
mkdir -p "$HOME/Documents/TimeCrisis2VR" "$HOME/devkit-game/TimeCrisis2VR"
chmod 777 "$HOME/Documents/TimeCrisis2VR"
''')
        staged_apk = "devkit-game/TimeCrisis2VR/.TimeCrisis2VR.apk.new-" + stamp
        staged_rom = "Documents/TimeCrisis2VR/.timecrs2.zip.new-" + stamp
        run(scp + [str(APK), scp_host + ":" + staged_apk])
        run(scp + [str(args.rom), scp_host + ":" + staged_rom])
        apk_sha = digest(APK)
        rom_sha = digest(args.rom)
        remote("printf '%s\\n' " + shlex.quote(apk_sha + "  " + staged_apk) + " " + shlex.quote(rom_sha + "  " + staged_rom) + " | (cd \"$HOME\" && sha256sum -c -)")
        remote("set -eu; cd \"$HOME\"; chmod 644 " + shlex.quote(staged_rom) + "; mv " + shlex.quote(staged_rom) + " Documents/TimeCrisis2VR/timecrs2.zip; mv " + shlex.quote(staged_apk) + " devkit-game/TimeCrisis2VR/TimeCrisis2VR.apk")
        parms = {"gameid": TITLE, "directory": "/home/steamos/devkit-game/TimeCrisis2VR", "force_appid": "", "argv": ["TimeCrisis2VR.apk"], "env": {}, "settings": {"steam_play": "1", "compat_tool": "lepton"}, "lepton_args": ""}
        reply = remote('cd "$HOME/devkit-utils" && python3 steam-client-create-shortcut --parms ' + shlex.quote(json.dumps(parms)), capture=True)
        response = registration_response(reply)
        print("Steam registration completed" + (": " + response if response else "."))
        print("Installed and registered TimeCrisis2VR. Launch it from the Steam library.")
        print("Pre-install backup: /home/steamos/" + backup)
    finally:
        if lock is not None:
            run([str(lock), "release", holder])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true", help="verify the local bundle without device access")
    parser.add_argument("--dry-run", action="store_true", help="verify the ROM and report the installation plan")
    parser.add_argument("--install", action="store_true", help="perform the remote installation after verification")
    parser.add_argument("--host", default=os.environ.get("FRAME_HOST"), help="steamos@<Frame address>; SSH must be enabled on the Frame")
    parser.add_argument("--identity", default=os.environ.get("FRAME_IDENTITY"), help="SSH private key to use, e.g. the SteamOS Devkit Client's devkit_rsa")
    parser.add_argument("--host-key-alias", default=os.environ.get("FRAME_HOST_KEY_ALIAS", ""))
    parser.add_argument("--lock-script", default=os.environ.get("FRAME_LOCK_SCRIPT"), help="optional booking script for a shared Frame (claim/release)")
    parser.add_argument("--rom", type=Path, help="user-supplied Time Crisis II ZIP")
    args = parser.parse_args()
    verify_bundle()
    if args.verify_only:
        return
    if args.install or args.dry_run:
        install(args)
        return
    fail("choose --verify-only, --dry-run, or --install")


if __name__ == "__main__":
    main()
