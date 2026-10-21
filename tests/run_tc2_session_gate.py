"""Private two-core gate: solo -> negotiated link -> reconnect -> solo.
Run after building headless. Requires the user's extracted ROMs; writes build/.
"""
from pathlib import Path
import os,subprocess,time,json,hashlib
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/session-core-proof'
if out.exists():raise SystemExit('Preserve previous evidence; use a fresh build/session-core-proof directory')
out.mkdir(parents=True)
binary=ROOT/'build/headless/tc2_headless';jobs=[]
try:
    for side in ['red','blue']:
        d=out/side;d.mkdir();(d/'extracted').symlink_to(ROOT/'upstream/tc2/extracted')
        env={k:v for k,v in os.environ.items() if not k.startswith('TC2_')}
        env.update(TC2_SESSION='host' if side=='red' else '127.0.0.1',TC2_FRAMES='1500',TC2_PACE='1',TC2_JITTER_MS='3',TC2_LINK_DIAG='1',TC2_STOP_ON_TRAP='1',TC2_SHOTLIST=str(d)+':2200,3999',TC2_PRESS='1@1200+5,1@1230+5,1@1260+5,1@1290+5,10@1400+5,10@1800+5,10@2100+5,20@1500+2000')
        log=(d/'core.log').open('w');job=subprocess.Popen([str(binary),'reset','18446744073709551615'],cwd=d,env=env,stdout=log,stderr=log);jobs.append((job,log,d));time.sleep(.2)
    for p,_,_ in jobs:assert p.wait(timeout=240)==0
    from check_tc2_session_evidence import check
    check(out)
    (out/'report.json').write_text(json.dumps({'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'result':'two negotiated games then solo, both clean','source':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()},indent=2)+'\n')
    print('PASS: both real cores negotiated, played, reconnected, played again, returned solo; zero traps')
finally:
    for p,f,_ in jobs:
        if p.poll() is None:p.terminate();p.wait()
        f.close()
