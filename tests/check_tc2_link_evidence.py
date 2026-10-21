"""Check real-core evidence, beyond socket activity. Captures still need visual review.
Usage: python3 tests/check_tc2_link_evidence.py build/link-pair-paced-final
"""
from pathlib import Path
import argparse, hashlib, json, re

def check(directory):
    report=json.loads((directory/'report.json').read_text())
    assert report['linked'] and len(report['runs'])==2
    wire={}; captures=[]
    for run in report['runs']:
        assert run['exit']==0,run
        folder=directory/run['side']; log=(folder/'core.log').read_text()
        assert re.search(r'\[TC2\].*0 traps, 0 FP exceptions',log),run['side']
        summary=re.search(r'\[LINK\] summary tx=(\d+) rx=(\d+) accepted=(\d+) validators=(\d+)',log)
        assert summary and all(int(x)>0 for x in summary.groups()),run['side']
        states=[dict(re.findall(r'(\w+)=(\d+)',line)) for line in log.splitlines() if line.startswith('[LINKSTATE]')]
        game=[s for s in states if s.get('mode')=='2']
        assert game,('original ROM never reached two-cabinet game mode',run['side'])
        assert all(s['fresh']=='4' and int(s['drift'])<17 for s in game),('original ROM reports stale partner during game mode',run['side'])
        packets=[json.loads(line) for line in (folder/'packets.jsonl').read_text().splitlines()]
        wire[run['side']]={kind:[p['data'] for p in packets if p['event']==kind] for kind in ('tx','rx')}
        final=folder/f"f{report['frames']-1:05}.ppm"
        digest=hashlib.sha256(final.read_bytes()).hexdigest()
        assert digest==run['captures'][final.name]
        captures.append(digest)
    red,blue=wire['red'],wire['blue']
    assert red['rx']==blue['tx'][:len(red['rx'])], 'red did not receive blue original bytes in order'
    assert blue['rx']==red['tx'][:len(blue['rx'])], 'blue did not receive red original bytes in order'
    assert captures[0]!=captures[1], 'final cabinet renders are identical'
    print('PASS: both real cores clean; ROM accepts packets and reaches fresh two-cabinet game mode; original wire bytes preserved; distinct final captures')
    print('Visual review is still required to confirm gameplay and independent controls.')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);check(p.parse_args().directory)
