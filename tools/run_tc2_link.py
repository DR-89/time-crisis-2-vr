"""Run independent real TC2 cores; private ROMs and evidence stay under build/.

This is a diagnostic runner, not a claim that the linked gameplay gate passed.
Inspect report.json, ROM acceptance counters and both software captures.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, socket, subprocess, time
ROOT = Path(__file__).resolve().parents[1]

def run(output, frames, linked, press, paced=False, blue_press=None):
    output=output.resolve()
    if output.exists() and any(output.iterdir()): raise RuntimeError("Use a fresh evidence directory; previous saves must not change the baseline")
    output.mkdir(parents=True,exist_ok=True)
    binary=ROOT/'build/headless/tc2_headless'
    roms=ROOT/'upstream/tc2/extracted'
    if not binary.is_file() or not roms.is_dir():
        raise RuntimeError('Build headless target and prepare the user ROM first')
    with socket.socket() as probe:
        probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]
    binary_sha=hashlib.sha256(binary.read_bytes()).hexdigest()
    jobs=[]; start=time.monotonic()
    try:
        for side in range(2 if linked else 1):
            directory=output/('red' if side==0 else 'blue'); directory.mkdir(exist_ok=True)
            link=directory/'extracted'
            if not link.exists(): link.symlink_to(roms,target_is_directory=True)
            env={k:v for k,v in os.environ.items() if not k.startswith('TC2_')}
            env.update(TC2_FRAMES=str(frames),TC2_LINK_DIAG='1',TC2_BACKUP=str(directory/'backup.nv'),
                       TC2_SHOTLIST=str(directory)+':'+','.join(str(x) for x in (600,1000,1400,1800,2200,frames-1) if x<frames),
                       TC2_PRESS=(blue_press if side and blue_press is not None else press),TC2_STOP_ON_TRAP='1')
            if linked:
                env.update(TC2_LINK=f'listen:{port}' if side==0 else f'connect:127.0.0.1:{port}',
                           TC2_LINK_SIDE=str(side),TC2_LINK_SYNC='0' if paced else '1',TC2_LINK_TRACE=str(directory/'packets.jsonl'))
            if paced: env.update(TC2_PACE='1',TC2_SKEW_MS=str(side*150),TC2_JITTER_MS='3')
            log=(directory/'core.log').open('w')
            job=subprocess.Popen([str(binary),'reset','18446744073709551615'],cwd=directory,env=env,stdout=log,stderr=log)
            jobs.append((job,log,directory))
            if linked and side==0:
                deadline=time.monotonic()+10
                while '[LINK] waiting' not in (directory/'core.log').read_text():
                    if job.poll() is not None or time.monotonic()>deadline: raise RuntimeError('Listener failed to start')
                    time.sleep(.05)
        for job,log,directory in jobs:
            job.wait(timeout=max(1,600-(time.monotonic()-start)))
        report={'linked':linked,'frames':frames,'paced':paced,'seconds':round(time.monotonic()-start,3),
                'binary_sha256':binary_sha,
                'source_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT)),
                'source':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                'runs':[{'side':d.name,'exit':p.returncode,'log':str(d/'core.log')} for p,l,d in jobs]}
        for entry,(_,_,directory) in zip(report['runs'],jobs):
            logtext=(directory/'core.log').read_text()
            entry['summaries']=re.findall(r'^\[(?:LINK|TC2)\] (?:summary|[0-9]+ instructions).*$',logtext,re.M)
            entry['captures']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(directory.glob('*.ppm'))}
        (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))
        if any(p.returncode for p,l,d in jobs): raise RuntimeError('Core run failed; inspect logs')
    finally:
        for job,log,directory in jobs:
            if job.poll() is None: job.terminate(); job.wait()
            log.close()

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--output',type=Path,default=ROOT/'build/link-evidence'); p.add_argument('--frames',type=int,default=2400)
    p.add_argument('--solo',action='store_true'); p.add_argument('--paced',action='store_true'); p.add_argument('--blue-press'); p.add_argument('--press',default='1@1200+5,1@1230+5,1@1260+5,1@1290+5,10@1400+5,20@1500+1200')
    a=p.parse_args(); run(a.output,a.frames,not a.solo,a.press,a.paced,a.blue_press)
