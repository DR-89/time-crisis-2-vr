"""Inspect local frame snapshots through the production GLES renderer on ANGLE.

Frames and assets contain ROM-derived data; keep input and output in ignored
local folders. This also verifies that the real GLES shaders compile and draw.
"""
from pathlib import Path
import argparse,ctypes as C,os,re,shutil,subprocess
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--frames',type=Path,default=ROOT/'build/transition-frames');p.add_argument('--output',type=Path,required=True)
p.add_argument('--select',default='*');p.add_argument('--flat',action='store_true');p.add_argument('--x',type=float,default=.12);p.add_argument('--y',type=float,default=.08);p.add_argument('--yaw',type=float,default=0);p.add_argument('--only-model',type=int)
p.add_argument('--baseline',help='Render the renderer/scene sources from this local Git revision')
p.add_argument('--verify-scale',action='store_true',help='Compare paused slider edits with a fresh geometry upload')
p.add_argument('--verify-filter',action='store_true',help='ROM-free palette, tile-edge, alpha and filter continuity checks')
p.add_argument('--geometry',action='store_true',help='Export local per-vertex calibration measurements')
p.add_argument('--scale',type=float,default=1);p.add_argument('--distance',type=float,default=0,help='Scene clearance in metres')
a=p.parse_args();out=ROOT/'build/tc2-render-test';out.mkdir(exist_ok=True);a.output.mkdir(parents=True,exist_ok=True)
if a.verify_scale and a.baseline:p.error('--verify-scale requires current renderer sources')
if a.verify_filter and (a.baseline or a.verify_scale):p.error('--verify-filter requires current sources without --verify-scale')
if a.baseline and (a.scale!=1 or a.distance!=0):p.error('Custom calibration requires current renderer sources')
if not .25<=a.scale<=4 or not 0<=a.distance<=1:p.error('Scale must be .25..4; distance 0..1 m')
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=Path(subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip())
angle=next((Path(os.environ['LOCALAPPDATA'])/'Programs/Microsoft VS Code').glob('*/libEGL.dll')).parent
headers=Path(os.environ.get('ANDROID_SDK_ROOT',ROOT/'.tools/android-sdk'))/'ndk'/os.environ.get('ANDROID_NDK_VERSION','28.2.13676358')/'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for folder in ('GLES3','KHR'):shutil.copytree(headers/folder,out/'include'/folder,dirs_exist_ok=True)
def run(lines):
    script=out/'compile.cmd';script.write_text('@echo off\ncall "'+str(vs/'VC/Auxiliary/Build/vcvars64.bat')+'" >nul\n'+'\n'.join(lines)+'\n')
    subprocess.run([os.environ['ComSpec'],'/c',str(script)],cwd=out,check=True)
run([f'dumpbin /nologo /exports "{angle / "libGLESv2.dll"}" > exports.txt'])
exports=re.findall(r'^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(gl\w+)\s*$',(out/'exports.txt').read_text(),re.M);assert len(exports)>100
(out/'gles.def').write_text('LIBRARY libGLESv2.dll\nEXPORTS\n'+'\n'.join(exports))
includes=' '.join('/I"'+str(q)+'"' for q in (out/'include',ROOT/'quest',ROOT/'upstream/tc2/src',ROOT/'build/pc/compat'))
source_paths=[ROOT/q for q in ('quest/tc2_gl.c','quest/tc2_scene.c','tests/tc2_renderer_fixture.c')]
if a.baseline:
    for i,name in enumerate(('tc2_gl.c','tc2_scene.c')):
        path=out/('baseline-'+name);path.write_bytes(subprocess.check_output(['git','show',f'{a.baseline}:quest/{name}'],cwd=ROOT));source_paths[i]=path
sources=' '.join('"'+str(q)+'"' for q in source_paths)
if a.baseline:includes+=' /DTC2_BASELINE'
run(['lib /nologo /def:gles.def /machine:x64 /out:gles.lib',f'cl /nologo /std:c11 /O2 /LD {includes} {sources} gles.lib /Fe:tc2-render.dll'])
directory=os.add_dll_directory(str(angle));egl=C.WinDLL(str(angle/'libEGL.dll'));lib=C.CDLL(str(out/'tc2-render.dll'));gles=C.WinDLL(str(angle/'libGLESv2.dll'))
P=C.c_void_p;I=C.c_int;U=C.c_uint;F=C.c_float
def bind(lib,name,result,*args):
    f=getattr(lib,name);f.restype=result;f.argtypes=args;return f
display=bind(egl,'eglGetDisplay',P,P)(None);assert bind(egl,'eglInitialize',U,P,P,P)(display,None,None)
attributes=(I*15)(0x3033,1,0x3040,0x40,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3025,24,0x3038)
config=P();count=I();assert bind(egl,'eglChooseConfig',U,P,P,P,I,P)(display,attributes,C.byref(config),1,C.byref(count)) and count.value
w,h=960,720
surface=bind(egl,'eglCreatePbufferSurface',P,P,P,P)(display,config,(I*5)(0x3057,w,0x3056,h,0x3038))
context=bind(egl,'eglCreateContext',P,P,P,P,P)(display,config,None,(I*3)(0x3098,3,0x3038))
current=bind(egl,'eglMakeCurrent',U,P,P,P,P);assert surface and context and current(display,surface,surface,context)
assert bind(lib,'tc2_fixture_init',I,C.c_char_p)(None if a.verify_filter else str(a.frames/'assets.bin').encode())
load=bind(lib,'tc2_fixture_load',I,C.c_char_p,I);draw=bind(lib,'tc2_fixture_draw',I,I,I,F,F,F,P);report=bind(lib,'tc2_fixture_report',I,C.c_char_p)
error=bind(gles,'glGetError',U);pixels=(C.c_ubyte*(w*h*3))();tiles=[]
frames=sorted({frame for pattern in a.select.split(',') for frame in a.frames.glob('frame-'+pattern+'.bin')})
if a.verify_filter:
    from texture_filter_checks import verify
    verify(lib,gles,bind,w,h,pixels,draw,error,a.output)
    frames=[]
else:assert frames,'No frame snapshots selected'
scale_changes=0
if not a.baseline:
    set_scale=bind(lib,'tc2_fixture_scale',None,F);set_distance=bind(lib,'tc2_fixture_distance',None,F)
    set_scale(a.scale);set_distance(a.distance)
for frame in frames:
    assert load(str(frame).encode(),a.flat),frame
    if a.only_model is not None:bind(lib,'tc2_fixture_only_model',None,I)(a.only_model)
    assert draw(w,h,a.x,a.y,a.yaw,pixels),frame
    assert error()==0,frame
    im=Image.frombytes('RGB',(w,h),bytes(pixels));im.save(a.output/(frame.stem+'.png'))
    assert report(str(a.output/(frame.stem+'.json')).encode())
    if a.geometry:assert bind(lib,'tc2_fixture_geometry',I,C.c_char_p)(str(a.output/(frame.stem+'.csv')).encode())
    thumb=im.resize((320,240));tile=Image.new('RGB',(320,264),(25,25,25));tile.paste(thumb,(0,24));ImageDraw.Draw(tile).text((8,6),frame.stem,fill='white');tiles.append(tile)
    if a.verify_scale:
        original=bytes(pixels)
        for size,distance in ((.25,0),(2,0),(4,0),(1,.4),(1,1),(.25,1),(4,1),(a.scale,a.distance)):
            set_scale(size);set_distance(distance)
            assert draw(w,h,a.x,a.y,a.yaw,pixels) and error()==0
            paused=bytes(pixels)
            assert load(str(frame).encode(),a.flat)
            if a.only_model is not None:bind(lib,'tc2_fixture_only_model',None,I)(a.only_model)
            assert draw(w,h,a.x,a.y,a.yaw,pixels) and error()==0
            assert paused==bytes(pixels),'Paused scale change must match a fresh frame upload'
            if a.flat or (size,distance)==(a.scale,a.distance):assert paused==original,'Flat projection and restoring settings must preserve the picture'
            elif paused!=original:scale_changes+=1
        Image.frombytes('RGB',(w,h),paused).save(a.output/(frame.stem+'-restored.png'))
if a.verify_scale and not a.flat:assert scale_changes,'Choose a visible world frame with an offset viewpoint'
for start in range(0,len(tiles),20):
    subset=tiles[start:start+20];sheet=Image.new('RGB',(1280,264*((len(subset)+3)//4)),(25,25,25))
    for i,tile in enumerate(subset):sheet.paste(tile,((i%4)*320,(i//4)*264))
    sheet.save(a.output/f'contact-{start//20}.png')
current(display,None,None,None);bind(egl,'eglDestroyContext',U,P,P)(display,context);bind(egl,'eglDestroySurface',U,P,P)(display,surface);bind(egl,'eglTerminate',U,P)(display)
print(f'PASS: {len(frames)} production renderer frames; no GLES errors. Captures in {a.output}')
