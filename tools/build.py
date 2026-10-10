"""Quest build: ROM-free APK by default, optional APK bundling supplied ROMs."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, tempfile, zipfile
import xml.etree.ElementTree as ET
import bootstrap, prepare_tc2
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build'
JAVA=Path(os.environ.get('JAVA_HOME',r'C:\Program Files\Android\Android Studio\jbr'))/'bin'
def run(args,cwd=ROOT):
    print('> '+' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),cwd=cwd,check=True)

def prepare(rom):
    return prepare_tc2.prepare(rom)

def prepare_sound():
    pass

def package_manifest(bundle_roms):
    """The OpenXR activity must retain its own VR categories in both variants."""
    ET.register_namespace('android','http://schemas.android.com/apk/res/android')
    tree=ET.parse(ROOT/'quest/AndroidManifest.xml');ns='{http://schemas.android.com/apk/res/android}'
    activities={a.get(ns+'name'):a for a in tree.getroot().find('application').findall('activity')}
    main=activities['.MainActivity'];setup=activities['.LauncherActivity']
    categories={c.get(ns+'name') for c in main.findall('intent-filter/category')}
    assert {'android.intent.category.LAUNCHER','org.khronos.openxr.intent.category.IMMERSIVE_HMD','com.oculus.intent.category.VR'}<=categories
    if not bundle_roms:
        # Setup is a separate 2D task. Its handoff targets the VR-marked activity.
        for intent in main.findall('intent-filter'):
            for category in list(intent.findall('category')):
                if category.get(ns+'name')=='android.intent.category.LAUNCHER':intent.remove(category)
        intent=ET.SubElement(setup,'intent-filter')
        ET.SubElement(intent,'action',{ns+'name':'android.intent.action.MAIN'})
        ET.SubElement(intent,'category',{ns+'name':'android.intent.category.LAUNCHER'})
    output=BUILD/('AndroidManifest-bundled.xml' if bundle_roms else 'AndroidManifest-rom-free.xml')
    tree.write(output,encoding='utf-8',xml_declaration=True)
    return output

def package(assets):
    models=assets/'models';models.mkdir(exist_ok=True)
    model=ROOT/'quest/assets/models/player-gun.tcgun'
    if not model.is_file():raise RuntimeError('Missing player gun asset: '+str(model))
    shutil.copy2(model,models/model.name)
    (assets/'models.sha256').write_text(hashlib.sha256(model.read_bytes()).hexdigest()+'  '+model.name+'\n',encoding='ascii')
    bt=ROOT/'.tools/android-sdk/build-tools/35.0.1';android=ROOT/'.tools/android-sdk/platforms/android-35/android.jar'
    classes=BUILD/'classes';classes.mkdir(exist_ok=True)
    sources=list((ROOT/'.tools/sdl/SDL2-2.30.11/android-project/app/src/main/java').rglob('*.java'))+list((ROOT/'quest/java').rglob('*.java'))
    argfile=BUILD/'javac.args'
    argfile.write_text('\n'.join('"'+str(p).replace('\\','/')+'"' for p in sources))
    run([JAVA/'javac.exe','-source','8','-target','8','-classpath',android,'-d',classes,'@'+str(argfile)])
    jar=BUILD/'classes.jar';run([JAVA/'jar.exe','--create','--file',jar,'-C',classes,'.'])
    dex=BUILD/'dex';dex.mkdir(exist_ok=True)
    run([JAVA/'java.exe','-cp',bt/'lib/d8.jar','com.android.tools.r8.D8','--lib',android,'--min-api','29','--output',dex,jar])
    licenses=assets/'licenses';licenses.mkdir(exist_ok=True)
    for source,name in [(ROOT/'LICENSE','TimeCrisis2VR-LICENSE.txt'),(ROOT/'NOTICE.md','NOTICE.md'),(ROOT/'upstream/LICENSE','namco22-LICENSE.txt'),(ROOT/'.tools/sdl/SDL2-2.30.11/LICENSE.txt','SDL2-LICENSE.txt'),(ROOT/'.tools/openxr/META-INF/LICENSE','OpenXR-LICENSE.txt')]:
        shutil.copy2(source,licenses/name)
    base=BUILD/'unaligned.apk'
    # Whitelist public assets into a fresh directory; stale build ROMs cannot leak.
    with tempfile.TemporaryDirectory(prefix='public-assets-',dir=BUILD) as temp:
        public=Path(temp)
        for name in ('models','licenses'):shutil.copytree(assets/name,public/name)
        shutil.copy2(assets/'models.sha256',public/'models.sha256')
        if args.bundle_roms:shutil.copytree(assets/'roms',public/'roms')
        # c71.bin is optional and already supplied by the upstream engine.
        manifest=[line for line in (assets/'roms.sha256').read_text().splitlines() if line.split('  ',1)[1]!='c71.bin']
        assert len(manifest)==22
        (public/'roms.sha256').write_text('\n'.join(manifest)+'\n',encoding='ascii')
        run([bt/'aapt2.exe','link','--manifest',package_manifest(args.bundle_roms),'-I',android,'-A',public,'-o',base])
    strip=ROOT/'.tools/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
    libs=BUILD/'package-libs';libs.mkdir(exist_ok=True)
    for name,source in [('libmain.so',BUILD/'native/libmain.so'),('libSDL2.so',BUILD/'native/sdl/libSDL2.so'),('libopenxr_loader.so',ROOT/'.tools/openxr/prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so')]:
        run([strip,'--strip-unneeded','-o',libs/name,source])
    with zipfile.ZipFile(base,'a',zipfile.ZIP_DEFLATED) as z:
        if not args.bundle_roms:assert not any(n.startswith('assets/roms/') or n.endswith('timecrs2.zip') for n in z.namelist())
        for p in dex.glob('*.dex'):z.write(p,p.name)
        for p in libs.glob('*.so'):z.write(p,'lib/arm64-v8a/'+p.name)
    output=ROOT/('artifacts/bundled/TimeCrisis2VR-with-ROM.apk' if args.bundle_roms else 'artifacts/TimeCrisis2VR-quest3-debug.apk');output.parent.mkdir(parents=True,exist_ok=True)
    run([bt/'zipalign.exe','-f','-P','16','4',base,output])
    key=ROOT/'.tools/debug.keystore'
    if not key.exists():run([JAVA/'keytool.exe','-genkeypair','-keystore',key,'-storepass','android','-alias','androiddebugkey','-keypass','android','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=Android Debug,O=Android,C=US'])
    signer=[JAVA/'java.exe','-jar',bt/'lib/apksigner.jar']
    run(signer+['sign','--ks',key,'--ks-key-alias','androiddebugkey','--ks-pass','pass:android','--key-pass','pass:android',output])
    run(signer+['verify','--verbose',output]);run([bt/'zipalign.exe','-c','-P','16','4',output])
    report={'apk':output.name,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'bytes':output.stat().st_size,'upstream':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT/'upstream',text=True).strip(),'abi':'arm64-v8a','roms_bundled':args.bundle_roms,'rom_sha256':hashlib.sha256(args.rom.read_bytes()).hexdigest(),'hardware_tested':False}
    manifest=ET.parse(ROOT/'quest/AndroidManifest.xml').getroot();ns='{http://schemas.android.com/apk/res/android}'
    report['version_name']=manifest.get(ns+'versionName');report['version_code']=int(manifest.get(ns+'versionCode'))
    (output.parent/'build-info.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--rom',type=Path,default=Path.home()/'Downloads/timecrs2.zip');p.add_argument('--jobs',type=int,default=2);p.add_argument('--package-only',action='store_true');p.add_argument('--skip-prepare',action='store_true');p.add_argument('--bundle-roms',action='store_true',help='Include the supplied ROM files in the APK');args=p.parse_args()
    if not args.rom.is_file():p.error('ROM not found: '+str(args.rom))
    BUILD.mkdir(exist_ok=True)
    if not args.package_only:
        for item in bootstrap.PACKAGES[3:]:bootstrap.fetch(item)
        assets=BUILD/'assets' if args.skip_prepare else prepare(args.rom)
        prepare_sound()
        ndk=ROOT/'.tools/android-sdk/ndk/28.2.13676358'
        run(['cmake','-Wno-dev','-Wno-deprecated','-S',ROOT/'quest','-B',BUILD/'native','-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ROOT/'.tools/ninja/ninja.exe'),'-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-29','-DANDROID_STL=c++_static','-DCMAKE_BUILD_TYPE=Release'])
        run(['cmake','--build',BUILD/'native','--parallel',args.jobs])
    else:assets=BUILD/'assets'
    package(assets)
