"""Exercise production dlopen/function-table bridge against a fake runtime ABI."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1];out=root/'build/openvr-keyboard';out.mkdir(parents=True,exist_ok=True)
lib=out/'fake-openvr.so';exe=out/'fixture'
flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-g']
subprocess.run(flags+['-shared','-fPIC',str(root/'tests/openvr_keyboard_fake.c'),'-o',str(lib)],check=True)
subprocess.run(flags+['-DTCVR_STEAM_FRAME','-DQOVR_LIBRARY_PATH="'+str(lib)+'"',str(root/'tests/openvr_keyboard_fixture.c'),str(root/'quest/quest_openvr_keyboard.c'),'-o',str(exe)],check=True)
for scenario in range(6):subprocess.run([str(exe),str(lib),str(scenario)],check=True)

ui=out/'ui-fixture'
subprocess.run(flags+['-DTCVR_STEAM_FRAME','-DQOVR_LIBRARY_PATH="'+str(lib)+'"','-I'+str(root/'build/steam-frame/native/compat'),'-I'+str(root/'upstream/tc2/src'),'-I'+str(root/'link'),str(root/'tests/openvr_keyboard_ui_fixture.c'),str(root/'quest/quest_keyboard.c'),str(root/'quest/quest_openvr_keyboard.c'),str(root/'link/tc2_link.c'),str(root/'tests/link_host_stubs.c'),'-o',str(ui)],check=True)
subprocess.run([str(ui),str(lib),str(out)],check=True)

import struct,zlib
def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data))
p=out/'keyboard-done.ppm';rgb=p.read_bytes().split(b'\n',3)[3]
rows=b''.join(b'\0'+rgb[y*640*3:(y+1)*640*3] for y in range(480))
p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',640,480,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
