"""Overlay generation is deterministic and never edits the pinned submodule."""
from pathlib import Path
import importlib.util, subprocess, tempfile, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tc2_overlay',ROOT/'tools/tc2_overlay.py')
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
before=subprocess.check_output(['git','diff','--binary'],cwd=ROOT/'upstream')
with tempfile.TemporaryDirectory(prefix='tc2-overlay-') as tmp:
    out=Path(tmp)
    for headless in (False,True):
        module.generate(out,headless)
        once={p.name:p.read_bytes() for p in out.iterdir()}
        module.generate(out,headless)
        assert once=={p.name:p.read_bytes() for p in out.iterdir()}
        assert b'tc2_link_read' in once['tc2_chips.c']
        assert b'tc2_link_side() << 2' in once['tc2_h8.c']
assert subprocess.check_output(['git','diff','--binary'],cwd=ROOT/'upstream')==before
print('PASS: deterministic overlays; upstream unchanged')
