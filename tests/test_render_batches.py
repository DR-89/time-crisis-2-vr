"""Compare deferred GLES draws with the immediate reference on local ANGLE.

By default, compare queued draws with the same adapter flushing immediately.
An older renderer can also be passed explicitly, e.g. build/quest_gl-baseline.c.
The fixture covers texture mutation, blend/mask/fog, gamma, laser and two eye
transforms. Requires MSVC and the bootstrapped NDK headers.
"""
from pathlib import Path
import argparse, ctypes as C, os, re, shutil, subprocess

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--reference',type=Path);p.add_argument('--multiview',action='store_true');p.add_argument('--srgb',action='store_true',help='Compare a raw sRGB eye target with the original RGBA8 target');a=p.parse_args()
reference=a.reference.resolve() if a.reference else ROOT/'quest/quest_gl.c';assert reference.is_file()
out=ROOT/'build/render-test';out.mkdir(parents=True,exist_ok=True)
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=Path(subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip())
vcvars=vs/'VC/Auxiliary/Build/vcvars64.bat'
angle=next((Path(os.environ['LOCALAPPDATA'])/'Programs/Microsoft VS Code').glob('*/libEGL.dll')).parent
headers=ROOT/'.tools/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for folder in ('GLES3','KHR'):shutil.copytree(headers/folder,out/'include'/folder,dirs_exist_ok=True)
(out/'include/android').mkdir(exist_ok=True)
(out/'include/android/log.h').write_text('#define ANDROID_LOG_ERROR 6\nstatic int __android_log_print(int p,const char*t,const char*f,...){return 0;}\n')
def run_script(lines):
    path=out/'compile.cmd';path.write_text('@echo off\ncall "'+str(vcvars)+'" >nul\n'+'\n'.join(lines)+'\n')
    subprocess.run([os.environ['ComSpec'],'/c',str(path)],cwd=out,check=True)
run_script([f'dumpbin /nologo /exports "{angle / "libGLESv2.dll"}" > exports.txt'])
exports=re.findall(r'^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(gl\w+)\s*$',(out/'exports.txt').read_text(),re.M)
assert len(exports)>100
(out/'gles.def').write_text('LIBRARY libGLESv2.dll\nEXPORTS\n'+'\n'.join(exports))
run_script(['lib /nologo /def:gles.def /machine:x64 /out:gles.lib'])
for name,source in [('reference',reference),('deferred',ROOT/'quest/quest_gl.c')]:
    # An include wrapper avoids command-line quoting for macro string literals.
    wrapper=out/f'{name}.c'
    define=''
    if name=='reference':
        if not a.reference:define='#define QGL_TEST_IMMEDIATE\n'
        elif 'void qgl_flush(void)' not in reference.read_text():define='#define IMMEDIATE_REFERENCE\n'
    if 'void qgl_scene_begin(' in source.read_text():define+='#define TEST_SCENE_TARGET\n'
    if 'void qgl_atlas_texture(' in source.read_text():define+='#define TEST_ATLAS_ARRAY\n'
    if name=='deferred' and a.multiview:define+='#define TEST_MULTIVIEW\n'
    if name=='deferred' and a.srgb:define+='#define TEST_SRGB_TARGET\n'
    wrapper.write_text(define+f'#define RENDERER_SOURCE "{source.as_posix()}"\n#include "{(ROOT / "tests/render_fixture.c").as_posix()}"\n')
    run_script([f'cl /nologo /std:c11 /O2 /LD /I"{out / "include"}" /I"{ROOT / "quest/include"}" /I"{ROOT / "quest"}" /I"{ROOT / "upstream/engine"}" "{wrapper}" gles.lib /Fe:{name}.dll'])
directory=os.add_dll_directory(str(angle));egl=C.WinDLL(str(angle/'libEGL.dll'))
P=C.c_void_p;I=C.c_int;U=C.c_uint
def bind(lib,name,result,*args):
    f=getattr(lib,name);f.restype=result;f.argtypes=args;return f
display=bind(egl,'eglGetDisplay',P,P)(None)
assert bind(egl,'eglInitialize',U,P,P,P)(display,None,None)
attributes=(I*13)(0x3033,1,0x3040,0x40,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038)
config=P();count=I();assert bind(egl,'eglChooseConfig',U,P,P,P,I,P)(display,attributes,C.byref(config),1,C.byref(count)) and count.value
make_current=bind(egl,'eglMakeCurrent',U,P,P,P,P)
results={}
for name in ('reference','deferred'):
    lib=C.CDLL(str(out/f'{name}.dll'));render=bind(lib,'render_fixture',I,I,I,I,P)
    if name=='deferred' and a.multiview:
        proc=bind(egl,'eglGetProcAddress',P,C.c_char_p)(b'glFramebufferTextureMultiviewOVR')
        assert proc,'ANGLE has no multiview entry point'
        bind(lib,'render_fixture_proc',None,P)(proc)
    for variant in range(4):
        surface=bind(egl,'eglCreatePbufferSurface',P,P,P,P)(display,config,(I*5)(0x3057,256,0x3056,256,0x3038))
        context=bind(egl,'eglCreateContext',P,P,P,P,P)(display,config,None,(I*3)(0x3098,3,0x3038))
        assert surface and context and make_current(display,surface,surface,context)
        pixels=(C.c_ubyte*(256*256*4))();status=render(variant,256,256,pixels);assert status==1,(name,variant,status)
        results[name,variant]=bytes(pixels)
        make_current(display,None,None,None)
        bind(egl,'eglDestroyContext',U,P,P)(display,context);bind(egl,'eglDestroySurface',U,P,P)(display,surface)
for variant in range(4):
    expected=results['reference',variant];actual=results['deferred',variant]
    differences=sum(x!=y for x,y in zip(expected,actual))
    assert not differences,f'Eye {variant}: {differences} channel differences'
assert results['deferred',0]!=results['deferred',1],'Eye transforms produced identical images'
if not a.reference:
    oracle=bind(lib,'projection_fixture',I,I,I,I,P);projected=[]
    for variant in range(3):
        surface=bind(egl,'eglCreatePbufferSurface',P,P,P,P)(display,config,(I*5)(0x3057,256,0x3056,256,0x3038))
        context=bind(egl,'eglCreateContext',P,P,P,P,P)(display,config,None,(I*3)(0x3098,3,0x3038));assert make_current(display,surface,surface,context)
        pixels=(C.c_ubyte*(256*256*4))();assert oracle(variant,256,256,pixels)==1
        projected.append(bytes(pixels));make_current(display,None,None,None)
        bind(egl,'eglDestroyContext',U,P,P)(display,context);bind(egl,'eglDestroySurface',U,P,P)(display,surface)
    assert projected[0]!=projected[1],'Camera uniform did not change the flat layer'
    assert projected[1]==projected[2],'Flat sprites and reconstructed camera rays do not align off-centre'
    print('PASS: flat sprite grid matches reconstructed polygons pixel-for-pixel; old 500 px projection differs.')
# Production orange tracer: shared snapshot, stereo, fade, and no pixels at expiry.
from PIL import Image
lib=C.CDLL(str(out/'deferred.dll'));tracer=bind(lib,'tracer_fixture',I,I,I,I,I,P)
surface=bind(egl,'eglCreatePbufferSurface',P,P,P,P)(display,config,(I*5)(0x3057,512,0x3056,512,0x3038))
context=bind(egl,'eglCreateContext',P,P,P,P,P)(display,config,None,(I*3)(0x3098,3,0x3038));assert surface and context and make_current(display,surface,surface,context)
shots={}
for age,eye in [(0,0),(0,1),(32,0),(65,0),(120,0)]:
    pixels=(C.c_ubyte*(512*512*4))();assert tracer(age,eye,512,512,pixels)==1
    im=Image.frombytes('RGBA',(512,512),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGB');shots[age,eye]=im
    im.save(ROOT/f'artifacts/tracer-{age}-{eye}.png')
    changed=sum(p!=im.getpixel((0,0)) for p in im.get_flattened_data())
    assert changed>20 if age<65 else changed==0,(age,changed)
assert shots[0,0].tobytes()!=shots[0,1].tobytes()!=shots[32,0].tobytes()
assert any(r>g*2 and g>b*2 for r,g,b in shots[0,0].get_flattened_data()),'Tracer must be orange'
make_current(display,None,None,None);bind(egl,'eglDestroyContext',U,P,P)(display,context);bind(egl,'eglDestroySurface',U,P,P)(display,surface)
bind(egl,'eglTerminate',U,P)(display)
print('PASS: '+('raw sRGB target, ' if a.srgb else '')+('multiview' if a.multiview else 'deferred')+' both eyes, identity and non-identity gamma are byte-identical to reference; no GLES errors.')
