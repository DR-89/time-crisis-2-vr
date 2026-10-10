"""Build the native Windows OpenXR/desktop application and a portable package."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,zipfile
import xml.etree.ElementTree as ET
import bootstrap,bootstrap_pc,build

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--rom',type=Path,default=Path.home()/'Downloads/timecrs2.zip')
    parser.add_argument('--jobs',type=int,default=2)
    parser.add_argument('--skip-prepare',action='store_true')
    parser.add_argument('--output',type=Path,default=ROOT/'artifacts/pc/TimeCrisis2VR-PC',help='Portable folder (also determines the ZIP destination)')
    args=parser.parse_args()
    if not args.skip_prepare and not args.rom.is_file():parser.error('ROM ZIP not found')
    for package in [bootstrap.PACKAGES[3],bootstrap.PACKAGES[5],*bootstrap_pc.PACKAGES]:bootstrap.fetch(package)
    assets=ROOT/'build/assets' if args.skip_prepare else build.prepare(args.rom)
    build.prepare_sound()
    toolchain=ROOT/'.tools/llvm-mingw/llvm-mingw-20260922-ucrt-x86_64/bin'
    env=os.environ.copy();env['PATH']=str(toolchain)+os.pathsep+env['PATH']
    out=ROOT/'build/pc'
    def run(cmd):subprocess.run([str(x) for x in cmd],cwd=ROOT,env=env,check=True)
    run(['cmake','-S',ROOT/'pc','-B',out,'-G','Ninja',
         '-DCMAKE_MAKE_PROGRAM='+str(ROOT/'.tools/ninja/ninja.exe'),
         '-DCMAKE_C_COMPILER='+str(toolchain/'x86_64-w64-mingw32-clang.exe'),
         '-DCMAKE_CXX_COMPILER='+str(toolchain/'x86_64-w64-mingw32-clang++.exe'),
         '-DCMAKE_BUILD_TYPE=Release'])
    run(['cmake','--build',out,'--target','main','--parallel',args.jobs])
    package=args.output.resolve();package.mkdir(parents=True,exist_ok=True)
    shutil.copy2(out/'TimeCrisis2VR.exe',package/'TimeCrisis2VR.exe')
    shutil.copy2(ROOT/'.tools/openxr-pc/native/x64/release/bin/openxr_loader.dll',package/'openxr_loader.dll')
    shutil.copytree(assets/'roms',package/'extracted',dirs_exist_ok=True)
    shutil.copytree(ROOT/'quest/assets/models',package/'models',dirs_exist_ok=True)
    for name in ('LICENSE','NOTICE.md'):shutil.copy2(ROOT/name,package/name)
    licenses=package/'licenses';licenses.mkdir(exist_ok=True)
    for source,name in [(ROOT/'upstream/LICENSE','namco22.txt'),(ROOT/'.tools/sdl/SDL2-2.30.11/LICENSE.txt','SDL2.txt'),(ROOT/'.tools/zlib/zlib-1.3.1/LICENSE','zlib.txt')]:shutil.copy2(source,licenses/name)
    for source,name in [(ROOT/'pc/licenses/OpenXR.txt','OpenXR.txt'),(ROOT/'pc/vendor/glad/LICENSE','glad.txt'),(toolchain.parent/'LICENSE.TXT','LLVM.txt')]:shutil.copy2(source,licenses/name)
    runtime_licenses=toolchain.parent/'x86_64-w64-mingw32/share/mingw32'
    for source in runtime_licenses.glob('COPYING*'):shutil.copy2(source,licenses/source.name)
    (package/'Play VR.cmd').write_text('@echo off\ncd /d "%~dp0"\nstart "" TimeCrisis2VR.exe\n')
    (package/'Play Desktop.cmd').write_text('@echo off\ncd /d "%~dp0"\nstart "" TimeCrisis2VR.exe --desktop\n')
    shutil.copy2(ROOT/'pc/Start-SteamVR.ps1',package/'Start-SteamVR.ps1')
    (package/'Play SteamVR.cmd').write_text('@echo off\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-SteamVR.ps1"\n')
    if (ROOT/'docs/PCVR.md').exists():shutil.copy2(ROOT/'docs/PCVR.md',package/'README.md')
    manifest=ET.parse(ROOT/'quest/AndroidManifest.xml').getroot()
    version=manifest.get('{http://schemas.android.com/apk/res/android}versionName')
    info={'version':version,'platform':'Windows x64','renderer':'OpenGL 4.3 / OpenXR',
          'upstream':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT/'upstream',text=True).strip(),
          'roms_bundled':True,'validation_notes':'README.md',
          'exe_sha256':hashlib.sha256((package/'TimeCrisis2VR.exe').read_bytes()).hexdigest()}
    (package/'build-info.json').write_text(json.dumps(info,indent=2)+'\n')
    # Whitelist release files: local playtests may leave inputs, EEPROMs and logs
    # in the portable folder, but those must never enter a public download.
    files=[package/name for name in ('TimeCrisis2VR.exe','openxr_loader.dll','Play VR.cmd','Play Desktop.cmd','Play SteamVR.cmd','Start-SteamVR.ps1','README.md','LICENSE','NOTICE.md','build-info.json')]
    files+=list((package/'licenses').glob('*'))
    files+=[package/'extracted'/p.name for p in (assets/'roms').iterdir() if p.is_file()]
    files+=[package/'models'/p.name for p in (ROOT/'quest/assets/models').iterdir() if p.is_file()]
    sums=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(package).as_posix()+'\n' for p in sorted(files))
    (package/'SHA256SUMS.txt').write_text(sums,encoding='ascii');files.append(package/'SHA256SUMS.txt')
    archive=package.parent/f'TimeCrisis2VR-v{version.split("-")[0]}-windows-x64.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p in files:z.write(p,'TimeCrisis2VR-PC/'+p.relative_to(package).as_posix())
    archive.with_suffix('.zip.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n',encoding='ascii')
    print('Windows package: '+str(package))
    print('Windows ZIP: '+str(archive))

if __name__=='__main__':main()
