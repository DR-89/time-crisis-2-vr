"""Download pinned Android build dependencies into .tools (no global installation)."""
from pathlib import Path
import concurrent.futures, hashlib, json, urllib.request, zipfile

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / '.tools'
PACKAGES = [
    ('ndk', 'https://dl.google.com/android/repository/android-ndk-r27c-windows.zip', 'ac5f7762764b1f15341094e148ad4f847d050c38'),
    ('platform', 'https://dl.google.com/android/repository/platform-34-ext12_r01.zip', 'ba80ccbcc29b29f25ac926a08c0b2777f0bce842'),
    ('buildtools', 'https://dl.google.com/android/repository/build-tools_r35_windows.zip', 'af059bb67cf7786f45ee0db85e2d24985df1b4b6'),
    ('sdl', 'https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.zip', None),
    ('openxr', 'https://repo.maven.apache.org/maven2/org/khronos/openxr/openxr_loader_for_android/1.1.43/openxr_loader_for_android-1.1.43.aar', None),
    ('ninja', 'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip', None),
    # Online voice chat (docs/ONLINE.md): the Opus codec, BSD licence.
    ('opus', 'https://github.com/xiph/opus/archive/refs/tags/v1.5.2.zip', None),
]

def fetch(package):
    name, url, sha1 = package
    dest = TOOLS / name
    if (dest / '.complete').exists():
        return
    TOOLS.mkdir(exist_ok=True)
    archive = TOOLS / (name + '.zip')
    if not archive.exists():
        print('Downloading ' + name, flush=True)
        partial = archive.with_suffix('.part')
        urllib.request.urlretrieve(url, partial)
        partial.replace(archive)
    if sha1 and hashlib.sha1(archive.read_bytes()).hexdigest() != sha1:
        raise RuntimeError('Checksum mismatch: ' + str(archive))
    print('Extracting ' + name, flush=True)
    dest.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        z.extractall(dest)
    (dest / '.complete').write_text(url + '\n')

if __name__ == '__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(fetch, PACKAGES))
    print('Dependencies ready in ' + str(TOOLS))
