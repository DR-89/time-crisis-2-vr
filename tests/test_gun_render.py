"""Render the real gun with its production GLES code on ANGLE, including stereo."""
from pathlib import Path
import ctypes as C, os, re, shutil, subprocess, struct
from PIL import Image, ImageChops
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'build/gun-test';out.mkdir(parents=True,exist_ok=True)
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=Path(subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip())
angle=next((Path(os.environ['LOCALAPPDATA'])/'Programs/Microsoft VS Code').glob('*/libEGL.dll')).parent
headers=ROOT/'.tools/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for folder in ('GLES3','KHR'):shutil.copytree(headers/folder,out/'include'/folder,dirs_exist_ok=True)
def run(lines):
    script=out/'compile.cmd';script.write_text('@echo off\ncall "'+str(vs/'VC/Auxiliary/Build/vcvars64.bat')+'" >nul\n'+'\n'.join(lines)+'\n')
    subprocess.run([os.environ['ComSpec'],'/c',str(script)],cwd=out,check=True)
run([f'dumpbin /nologo /exports "{angle / "libGLESv2.dll"}" > exports.txt'])
exports=re.findall(r'^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(gl\w+)\s*$',(out/'exports.txt').read_text(),re.M);assert len(exports)>100
(out/'gles.def').write_text('LIBRARY libGLESv2.dll\nEXPORTS\n'+'\n'.join(exports))
run(['lib /nologo /def:gles.def /machine:x64 /out:gles.lib',f'cl /nologo /std:c11 /O2 /LD /I"{out / "include"}" /I"{ROOT / "quest"}" "{ROOT / "quest/quest_gun.c"}" "{ROOT / "tests/gun_fixture.c"}" gles.lib /Fe:gun.dll'])
directory=os.add_dll_directory(str(angle));egl=C.WinDLL(str(angle/'libEGL.dll'));lib=C.CDLL(str(out/'gun.dll'))
P=C.c_void_p;I=C.c_int;U=C.c_uint
def bind(lib,name,result,*args):
    f=getattr(lib,name);f.restype=result;f.argtypes=args;return f
display=bind(egl,'eglGetDisplay',P,P)(None);assert bind(egl,'eglInitialize',U,P,P,P)(display,None,None)
attributes=(I*15)(0x3033,1,0x3040,0x40,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3025,24,0x3038)
config=P();count=I();assert bind(egl,'eglChooseConfig',U,P,P,P,I,P)(display,attributes,C.byref(config),1,C.byref(count)) and count.value
current=bind(egl,'eglMakeCurrent',U,P,P,P,P);render=bind(lib,'gun_fixture',I,C.c_char_p,I,I,I,P)
images=[];w=h=640
for variant in range(26):
    surface=bind(egl,'eglCreatePbufferSurface',P,P,P,P)(display,config,(I*5)(0x3057,w,0x3056,h,0x3038))
    context=bind(egl,'eglCreateContext',P,P,P,P,P)(display,config,None,(I*3)(0x3098,3,0x3038));assert surface and context and current(display,surface,surface,context)
    pixels=(C.c_ubyte*(w*h*4))();status=render(str(ROOT/'quest/assets/models/player-gun.tcgun').encode(),variant,w,h,pixels);assert status==1,(variant,status)
    image=Image.frombytes('RGBA',(w,h),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGB');images.append(image)
    changed=sum(p!=image.getpixel((0,0)) for p in image.get_flattened_data());assert changed>1500,(variant,changed)
    image.save(ROOT/f'artifacts/gun-preview-{variant}.png')
    current(display,None,None,None);bind(egl,'eglDestroyContext',U,P,P)(display,context);bind(egl,'eglDestroySurface',U,P,P)(display,surface)
assert images[1].tobytes()!=images[2].tobytes(),'Missing stereo disparity'
assert images[2].tobytes()!=images[3].tobytes(),'Missing recoil/pose motion'
assert images[0].tobytes()!=images[5].tobytes(),'Slide must move in side view'
assert images[0].tobytes()==images[6].tobytes(),'Slide must return exactly to rest after 150 ms'
assert images[0].tobytes()!=images[7].tobytes(),'Trigger must move independently'
assert images[0].tobytes()!=images[8].tobytes(),'Accepted shots must show a local muzzle flash'
assert images[0].tobytes()==images[9].tobytes(),'Selecting an identical second asset must preserve its rendering'
assert images[0].tobytes()==images[25].tobytes(),'Shot animation must return to rest after 150 ms'
# Grip/body below the slide must remain stationary during recoil.
body=(0,370,w,h)
assert ImageChops.difference(images[0].crop(body),images[5].crop(body)).getbbox() is None,'Whole gun moved instead of its slide'
assert sum(p!=images[0].getpixel((0,0)) for p in images[0].crop(body).get_flattened_data())>1000,'Body comparison must contain gun pixels'
# A test-only second asset; never packaged as a replacement for the Tripo model.
second=bytearray((ROOT/'quest/assets/models/player-gun.tcgun').read_bytes());struct.pack_into('<f',second,24,-.3)
second_path=out/'second-slot-test.tcgun';second_path.write_bytes(second)
surface=bind(egl,'eglCreatePbufferSurface',P,P,P,P)(display,config,(I*5)(0x3057,64,0x3056,64,0x3038))
context=bind(egl,'eglCreateContext',P,P,P,P,P)(display,config,None,(I*3)(0x3098,3,0x3038));assert surface and context and current(display,surface,surface,context)
switch=bind(lib,'gun_switch_fixture',I,C.c_char_p,C.c_char_p)
assert switch(str(ROOT/'quest/assets/models/player-gun.tcgun').encode(),str(second_path).encode())==1
current(display,None,None,None);bind(egl,'eglDestroyContext',U,P,P)(display,context);bind(egl,'eglDestroySurface',U,P,P)(display,surface)
preview=[images[0]]*10+images[10:26]+[images[0]]*10
preview[0].save(ROOT/'artifacts/pistol-shot-preview.gif',save_all=True,append_images=preview[1:],duration=30,loop=0)
machine=bind(lib,'machine_gun_fixture',I,C.c_char_p,C.c_char_p,I,C.c_float,I,I,P)
smg_images={}
surface=bind(egl,'eglCreatePbufferSurface',P,P,P,P)(display,config,(I*5)(0x3057,w,0x3056,h,0x3038))
context=bind(egl,'eglCreateContext',P,P,P,P,P)(display,config,None,(I*3)(0x3098,3,0x3038));assert surface and context and current(display,surface,surface,context)
for view,age in [(i,-1) for i in range(5)]+[(0,a) for a in range(0,151,10)]:
    pixels=(C.c_ubyte*(w*h*4))()
    status=machine(str(ROOT/'quest/assets/models/player-gun.tcgun').encode(),str(ROOT/'quest/assets/models/machine-gun.tcgun').encode(),view,age,w,h,pixels)
    assert status==1,(view,age,status)
    im=Image.frombytes('RGBA',(w,h),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGB');smg_images[view,age]=im
    assert sum(p!=im.getpixel((0,0)) for p in im.get_flattened_data())>1500,(view,age)
    if age==-1:im.save(ROOT/f'artifacts/machine-gun-view-{view}.png')
assert smg_images[0,-1].tobytes()!=smg_images[4,-1].tobytes(),'Arcade state must select visibly different weapons'
assert smg_images[1,-1].tobytes()!=smg_images[2,-1].tobytes(),'Machine gun must have stereo disparity'
assert smg_images[0,-1].tobytes()!=smg_images[0,10].tobytes(),'Machine gun shot animation must move/flash'
assert smg_images[0,-1].tobytes()==smg_images[0,150].tobytes(),'Machine gun must return exactly to rest'
sequence=[smg_images[0,-1]]*10+[smg_images[0,a] for a in range(0,151,10)]+[smg_images[0,-1]]*10
sequence[0].save(ROOT/'artifacts/machine-gun-shot-preview.gif',save_all=True,append_images=sequence[1:],duration=30,loop=0)
# Compare the simplified silhouette against the same textured full source mesh.
reference=ROOT/'build/machine-gun-full.tcgun'
if reference.exists():
    import numpy as np
    pixels=(C.c_ubyte*(w*h*4))();assert machine(str(ROOT/'quest/assets/models/player-gun.tcgun').encode(),str(reference).encode(),0,-1,w,h,pixels)==1
    im=Image.frombytes('RGBA',(w,h),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGB');im.save(ROOT/'artifacts/machine-gun-reference.png')
    raw=np.asarray(im);opt=np.asarray(smg_images[0,-1]);background=raw[0,0]
    a=np.any(raw!=background,axis=2);b=np.any(opt!=background,axis=2);iou=np.sum(a&b)/np.sum(a|b)
    print(f'Machine-gun reference silhouette IoU: {iou:.4f}');assert iou>.97,'Simplification changed the silhouette excessively'
current(display,None,None,None);bind(egl,'eglDestroyContext',U,P,P)(display,context);bind(egl,'eglDestroySurface',U,P,P)(display,surface)
namco=bind(lib,'namco_fixture',I,C.c_char_p,I,C.c_float,I,I,P)
surface=bind(egl,'eglCreatePbufferSurface',P,P,P,P)(display,config,(I*5)(0x3057,w,0x3056,h,0x3038))
context=bind(egl,'eglCreateContext',P,P,P,P,P)(display,config,None,(I*3)(0x3098,3,0x3038));assert surface and context and current(display,surface,surface,context)
namco_images={}
for eye,age in [(i,-1) for i in range(3)]+[(0,18),(0,160)]:
    pixels=(C.c_ubyte*(w*h*4))();assert namco(str(ROOT/'quest/assets/models/namco-gun.tcgun').encode(),eye,age,w,h,pixels)==1
    im=Image.frombytes('RGBA',(w,h),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGB');namco_images[eye,age]=im
    assert sum(p!=im.getpixel((0,0)) for p in im.get_flattened_data())>1500
    im.save(ROOT/f'artifacts/namco-gun-{eye}-{int(age)}.png')
assert namco_images[1,-1].tobytes()!=namco_images[2,-1].tobytes()
assert namco_images[0,-1].tobytes()!=namco_images[0,18].tobytes()
assert namco_images[0,-1].tobytes()==namco_images[0,160].tobytes()
current(display,None,None,None);bind(egl,'eglDestroyContext',U,P,P)(display,context);bind(egl,'eglDestroySurface',U,P,P)(display,surface)
bind(egl,'eglTerminate',U,P)(display)
print('PASS: both production guns, arcade-state selection, stereo, recoil/slide/trigger, muzzle flash lifetime, fallback and stable aim; no GLES errors. Shot previews saved.')
