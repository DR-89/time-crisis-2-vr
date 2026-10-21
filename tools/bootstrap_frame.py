"""Fetch the small pinned Frame build inputs into ignored .tools directories."""
from pathlib import Path
import hashlib, shutil, tarfile, urllib.request, zipfile

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / '.tools'
SDL_URL = 'https://www.libsdl.org/release/SDL2-2.30.11.tar.gz'
SDL_SHA256 = '8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f'
XR_URL = 'https://repo.maven.apache.org/maven2/org/khronos/openxr/openxr_loader_for_android/1.1.43/openxr_loader_for_android-1.1.43.aar'
# Set from the pinned Maven artifact; the build refuses an unverified archive.
XR_SHA256 = '7e1b36141f9a4f1fa4a7e061936344fd9fcd36bce6c47eae2ad09812736167b6'

def fetch(url, target, expected):
    if target.exists():
        actual = hashlib.sha256(target.read_bytes()).hexdigest()
        if actual != expected: raise RuntimeError(f'Cached dependency checksum mismatch: {target}')
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_suffix(target.suffix + '.part')
    print('Downloading ' + url, flush=True)
    urllib.request.urlretrieve(url, partial)
    actual = hashlib.sha256(partial.read_bytes()).hexdigest()
    if actual != expected:
        partial.unlink(missing_ok=True)
        raise RuntimeError(f'Dependency checksum mismatch for {url}: {actual}')
    partial.replace(target)

def safe_extract_zip(archive, destination):
    root = destination.resolve()
    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            path = (destination / member.filename).resolve()
            if path != root and root not in path.parents: raise RuntimeError('Unsafe dependency archive path: ' + member.filename)
        z.extractall(destination)

def main():
    sdl_archive = TOOLS / 'downloads/SDL2-2.30.11.tar.gz'
    xr_archive = TOOLS / 'downloads/openxr_loader_for_android-1.1.43.aar'
    fetch(SDL_URL, sdl_archive, SDL_SHA256)
    fetch(XR_URL, xr_archive, XR_SHA256)
    sdl = TOOLS / 'sdl'; openxr = TOOLS / 'openxr'
    if not (sdl / 'SDL2-2.30.11/CMakeLists.txt').is_file():
        sdl.mkdir(parents=True, exist_ok=True)
        with tarfile.open(sdl_archive, 'r:gz') as archive:
            root = sdl.resolve()
            for member in archive.getmembers():
                path = (sdl / member.name).resolve()
                if path != root and root not in path.parents: raise RuntimeError('Unsafe SDL archive path: ' + member.name)
            archive.extractall(sdl)
    if not (openxr / 'prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so').is_file():
        if openxr.exists(): shutil.rmtree(openxr)
        openxr.mkdir(parents=True)
        safe_extract_zip(xr_archive, openxr)
    print('Frame dependencies ready (SDL 2.30.11 and Khronos OpenXR 1.1.43).')

if __name__ == '__main__': main()
