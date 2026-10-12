"""Validate the actual TCII Windows release and optionally replay a recording.

python tests/test_pc.py [--replay path/to/session.rec --nvram path/to/tc2_backup.nv]
Always uses a temporary extraction; the developer's settings and logs survive.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess, tempfile, zipfile

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--zip',type=Path,default=ROOT/'artifacts/pc/TimeCrisis2VR-v0.3.0-windows-x64.zip')
p.add_argument('--replay',type=Path)
p.add_argument('--nvram',type=Path)
p.add_argument('--side',type=int,choices=(1,2),default=1)
p.add_argument('--frames',type=int,default=28201)
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='tcvr-pc-test-') as temp:
    temp=Path(temp)
    with zipfile.ZipFile(a.zip) as z:
        assert not any(n.endswith(('.inputs','.rec','.log','.nv','.cfg','.keystore')) for n in z.namelist()),'Private state in release'
        z.extractall(temp)
    game=temp/'TimeCrisis2VR-PC'
    for line in (game/'SHA256SUMS.txt').read_text().splitlines():
        digest,name=line.split('  ',1)
        assert hashlib.sha256((game/name).read_bytes()).hexdigest()==digest,name
    assert (game/'TimeCrisis2VR.exe').read_bytes()[:2]==b'MZ'
    info=json.loads((game/'build-info.json').read_text())
    assert hashlib.sha256((game/'TimeCrisis2VR.exe').read_bytes()).hexdigest()==info['exe_sha256']
    assert 'Martoscar' in (game/'NOTICE.md').read_text(encoding='utf-8')
    env=os.environ.copy();env.update(TCVR_FAST='1',TCVR_TEST_HIDDEN='1',TCVR_CAPTURE_FRAME='1500',SDL_AUDIODRIVER='dummy')
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    subprocess.run([game/'TimeCrisis2VR.exe','--desktop','--no-dialog','--frames','1501'],env=env,startupinfo=startup,check=True,timeout=90)
    log=(game/'tc2.log').read_text()
    assert '[OPTIONS] laser ON' in log
    assert 'normal shutdown at frame 1501' in log and '0 traps, 0 FP exceptions' in log
    assert 'models/namco-gun.tcgun loaded: 13149 vertices, 17108 triangles' in log
    assert 'models/machine-gun.tcgun loaded:' in log
    assert not re.search(r'\[TRAP\]|\[GL\].*error|Shader:|fault [1-9]',log)
    image=(game/'eye-0.ppm').read_bytes().split(b'\n',3)[3]
    assert len(image)==1280*960*3 and sum(v>20 for v in image)>len(image)//30,'Blank desktop render'
    print('PASS: clean TCII Windows ZIP, hashes, model attribution, default laser, desktop GL and 1501 frames')
    if a.replay:
        shutil.copy2(a.replay,game/'regression.rec')
        if a.nvram:shutil.copy2(a.nvram,game/'tc2_backup.nv')
        (game/'quest-options.cfg').write_text(f'player_side={a.side}\n')
        subprocess.run([game/'TimeCrisis2VR.exe','--desktop','--no-dialog','--replay','regression.rec','--frames',str(a.frames)],env=env,startupinfo=startup,check=True,timeout=300)
        log=(game/'tc2.log').read_text()
        assert f'normal shutdown at frame {a.frames}' in log and '0 traps, 0 FP exceptions' in log
        assert '0 unknown C435 commands' in log
        if a.side==2:assert 'Robert route selected before original player initialization' in log
        assert not re.search(r'\[TRAP\]|fault [1-9]',log)
        (ROOT/f'build/pc-regression-p{a.side}.log').write_text(log)
        print(f'PASS: P{a.side} recording reaches {a.frames} frames without CPU/FP traps or unknown C435 commands')
