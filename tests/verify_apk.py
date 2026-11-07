"""Verify ROM integrity, the ARM64 ELF headers, manifest identity and exported entry point."""
from pathlib import Path
import argparse,os,hashlib,json,math,re,struct,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[1]
SDK=Path(os.environ.get('ANDROID_SDK_ROOT',ROOT/'.tools/android-sdk'))
BUILD_TOOLS=os.environ.get('ANDROID_BUILD_TOOLS_VERSION','35.0.1')
NDK_VERSION=os.environ.get('ANDROID_NDK_VERSION','28.2.13676358')
p=argparse.ArgumentParser();p.add_argument('--apk',type=Path,default=ROOT/'artifacts/bundled/TimeCrisis2VR-with-ROM.apk');args=p.parse_args()
apk=args.apk.resolve();info=json.loads(apk.with_name('build-info.json').read_text());bundled=info['roms_bundled']
with zipfile.ZipFile(apk) as z:
    assert z.testzip() is None
    manifest=z.read('assets/roms.sha256').decode().splitlines()
    assert len(manifest)==22, len(manifest)
    if not bundled:assert not any(n.startswith('assets/roms/') or n.endswith('timecrs2.zip') for n in z.namelist()),'ROM-free APK must not bundle ROMs'
    for line in manifest:
        digest,name=line.split('  ',1)
        data=z.read('assets/roms/'+name) if bundled else (ROOT/'build/assets/roms'/name).read_bytes()
        assert hashlib.sha256(data).hexdigest()==digest,name
    model_manifest=z.read('assets/models.sha256').decode().splitlines()
    assert {line.split('  ',1)[1] for line in model_manifest}=={'player-gun.tcgun','machine-gun.tcgun','namco-gun.tcgun'}
    for line in model_manifest:
        digest,name=line.split('  ',1);model=z.read('assets/models/'+name)
        assert hashlib.sha256(model).hexdigest()==digest,name
        assert model==(ROOT/'quest/assets/models'/name).read_bytes(),name
        assert model[:8] in (b'TCGUN001',b'TCGUN002')
        vertices,indices=struct.unpack_from('<2I',model,8)
        assert 0<vertices<=100000 and 0<indices<=300000 and indices%3==0
        if model[:8]==b'TCGUN001':
            w,h=struct.unpack_from('<2I',model,16);assert 0<w<=2048 and 0<h<=2048
            assert len(model)==36+vertices*32+indices*4+w*h*4
            offset,stride=36,32
            assert all(all(math.isfinite(x) for x in v) for v in struct.iter_unpack('<8f',model[offset:offset+vertices*stride]))
        else:
            offset,stride=28,40
            assert len(model)==offset+vertices*stride+indices*4
            parts=set()
            for v in struct.iter_unpack('<10f',model[offset:offset+vertices*stride]):
                assert all(math.isfinite(x) for x in v)
                assert all(0<=x<=1 for x in v[6:9]) and v[9] in (0,1,2)
                parts.add(v[9])
            assert parts=={0,1,2}
        assert all(math.isfinite(x) for x in struct.unpack_from('<3f',model,offset-12))
        assert max(struct.unpack_from(f'<{indices}I',model,offset+vertices*stride))<vertices
    libs=[n for n in z.namelist() if n.startswith('lib/')]
    assert sorted(libs)==['lib/arm64-v8a/libSDL2.so','lib/arm64-v8a/libmain.so','lib/arm64-v8a/libopenxr_loader.so']
    for lib in libs:
        elf=z.read(lib);assert elf[:5]==b'\x7fELF\x02';assert struct.unpack_from('<H',elf,18)[0]==183,lib
    assert z.read('classes.dex')[:4]==b'dex\n'
    for name in ['namco22-LICENSE.txt','SDL2-LICENSE.txt','OpenXR-LICENSE.txt']:assert z.read('assets/licenses/'+name)
    notice=z.read('assets/licenses/NOTICE.md').decode()
    assert 'Martoscar' in notice and 'https://creativecommons.org/licenses/by/4.0/' in notice
    assert '15fbd5b9add94a34b5c21746e3dd32be' in notice
badging=subprocess.check_output([str(SDK/'build-tools'/BUILD_TOOLS/'aapt.exe'),'dump','badging',str(apk)],text=True)
assert "package: name='org.timecrisis2.arcadevr'" in badging
entry='MainActivity' if bundled else 'LauncherActivity'
assert f"launchable-activity: name='org.timecrisis2.arcadevr.{entry}'" in badging
tree=subprocess.check_output([str(SDK/'build-tools'/BUILD_TOOLS/'aapt.exe'),'dump','xmltree',str(apk),'AndroidManifest.xml'],text=True)
activities=re.findall(r'(?ms)^([ ]+)E: activity\b(.*?)(?=^\1E: |\Z)',tree)
main=next(block for _,block in activities if '=".MainActivity"' in block)
setup=next(block for _,block in activities if '=".LauncherActivity"' in block)
for category in ('org.khronos.openxr.intent.category.IMMERSIVE_HMD','com.oculus.intent.category.VR'):
    assert category in main,'The actual OpenXR activity must be marked immersive'
    assert category not in setup,'Setup must not masquerade as the immersive game'
assert 'org.timecrisis2.arcadevr.setup' in setup,'Setup needs a separate Android task'
assert ('android.intent.category.LAUNCHER' in main)==bundled
assert "native-code: 'arm64-v8a'" in badging
assert 'quest2|quest3|quest3s' in tree,'Quest 2 must not fall back to an older-device compatibility profile'
readelf=SDK/'ndk'/NDK_VERSION/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
symbols=subprocess.check_output([str(readelf),'--dyn-syms',str(ROOT/'build/package-libs/libmain.so')],text=True)
assert ' SDL_main' in symbols
assert info['sha256']==hashlib.sha256(apk.read_bytes()).hexdigest()
print(f'APK verified: bundled={bundled}, 22 chip hashes, all three weapon assets, ARM64/DEX/SDL_main, {entry} launcher, actual game marked immersive, isolated setup task, model attribution, licenses and hash')
