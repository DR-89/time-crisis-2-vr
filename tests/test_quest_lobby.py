"""Production raster and platform text-event IP model. Output peers are synthetic."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'build/lobby-render';out.mkdir(exist_ok=True)
exe=out/'fixture'
command=['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-g','-I'+str(ROOT/'link'),'-I'+str(ROOT/'upstream/tc2/src'),'-I'+str(ROOT/'build/steam-frame/native/compat'),str(ROOT/'tests/quest_lobby_fixture.c'),str(ROOT/'quest/quest_keyboard.c'),str(ROOT/'quest/quest_openvr_keyboard.c'),str(ROOT/'link/tc2_link.c'),str(ROOT/'tests/link_host_stubs.c'),'-o',str(exe)]
for flags in ([],['-DTCVR_PC'],['-DTCVR_STEAM_FRAME']):
    subprocess.run(command+flags,check=True)
    subprocess.run([str(exe),str(out)],check=True)
import struct,zlib
def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data))
for p in out.glob('*.ppm'):
    rgb=p.read_bytes().split(b'\n',3)[3]
    rows=b''.join(b'\0'+rgb[y*640*3:(y+1)*640*3] for y in range(480))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',640,480,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
    p.with_suffix('.png').write_bytes(png)
print('Production raster captures:',out)
