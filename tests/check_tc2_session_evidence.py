"""Verify each real-game/reset segment independently, plus final solo save path."""
from pathlib import Path
import re,sys

def check(out):
    for side in ('red','blue'):
        d=out/side;log=(d/'core.log').read_text();segments=log.split('[SESSIONGATE] game ')
        assert len(segments)==3,(side,'expected two completed games')
        for number,segment in enumerate(segments[:2],1):
            rows=[dict(re.findall(r'(\w+)=(\d+)',line)) for line in segment.splitlines() if line.startswith('[LINKSTATE]')]
            playing=[row for row in rows if row['mode']=='2']
            assert playing,(side,number,'never entered linked game mode')
            assert all(row['fresh']=='4' and int(row['drift'])<17 for row in playing),(side,number,'stale partner')
            assert max(int(row['accepted']) for row in playing)>0,(side,number,'no ROM accepted packets')
        final=segments[2]
        assert 'summary tx=0 rx=0' in final,(side,'solo transport still active')
        assert '0 traps, 0 FP exceptions' in final,(side,'unclean core exit')
        assert re.search(r'\[NV\] (?:no tc2_backup.nv yet|backup RAM loaded from tc2_backup.nv)',final),(side,'solo save path not restored')
        assert (d/'tc2_backup.nv').is_file(),side
        assert (d/f'tc2_link_{side}.nv').is_file(),side
    print('PASS: both games independently reached fresh linked mode with ROM-accepted packets; clean solo return and isolated saves')

if __name__=='__main__':check(Path(sys.argv[1]))
