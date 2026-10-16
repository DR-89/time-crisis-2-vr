"""Compile and exercise the actual shared Quest/PCVR host with a mock XR runtime.
Uses the local LLVM/MinGW toolchain and headers bootstrapped by Build-PC.ps1.
"""
from pathlib import Path
import subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
compiler=ROOT/'.tools/llvm-mingw/llvm-mingw-20260922-ucrt-x86_64/bin/x86_64-w64-mingw32-clang.exe'
with tempfile.TemporaryDirectory(prefix='tcvr-handedness-') as temp:
    exe=Path(temp)/'handedness.exe'
    includes=['link','quest','quest/include','pc/vendor/glad/include','build/pc/compat',
              'upstream/tc2/src','upstream/tc2/src/eng','upstream/tc2/gen',
              'upstream/tc2/include','.tools/openxr-pc/include']
    subprocess.run([str(compiler),'-std=c11','-O1','-g','-Wall','-Wextra',
                    '-Wno-unused-function','-Wno-unused-variable','-Wno-missing-field-initializers',
                    '-DTCVR','-DTCVR_PC','-DSDL_MAIN_HANDLED','-DXR_USE_PLATFORM_WIN32',
                    '-DXR_USE_GRAPHICS_API_OPENGL','-include',str(ROOT/'upstream/tc2/include/win_compat.h'),
                    *['-I'+str(ROOT/p) for p in includes],
                    str(ROOT/'tests/handedness_fixture.c'),str(ROOT/'quest/quest_options.c'),
                    str(ROOT/'quest/quest_cover.c'),str(ROOT/'quest/quest_lobby.c'),str(ROOT/'link/tc2_link.c'),str(ROOT/'tests/link_host_stubs.c'),'-lws2_32','-o',str(exe)],check=True)
    subprocess.run([str(exe)],cwd=temp,check=True,timeout=15)
