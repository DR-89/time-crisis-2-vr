"""Validate the supplied arcade ROM and generate the two native H8 programs."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys, zipfile, zlib
ROOT=Path(__file__).resolve().parents[1]
TC=ROOT/'upstream/tc2'
PIN='0837a397ebe554dbf5341d95629aa1e29fc58a52'

def prepare(rom):
    actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=TC,text=True).strip()
    if actual!=PIN:raise RuntimeError('Unexpected upstream revision: '+actual)
    # Read the upstream table without executing its command-line program.
    import ast
    tree=ast.parse((TC/'tools/setup_roms.py').read_text(encoding='utf-8'))
    chips=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='CHIPS' for t in n.targets))
    extracted=TC/'extracted';extracted.mkdir(exist_ok=True)
    with zipfile.ZipFile(rom) as z:
        sources={}
        for entry in z.infolist():
            if entry.is_dir():continue
            key=(entry.file_size,entry.CRC)
            if key in chips.values():sources.setdefault(key,[]).append(entry)
        manifest=[]
        for name,(size,crc) in chips.items():
            candidates=sources.get((size,crc),[])
            if not candidates:raise RuntimeError('Missing required chip: '+name)
            entry=next((x for x in candidates if Path(x.filename).name==name),candidates[0])
            data=z.read(entry)
            if len(data)!=size or zlib.crc32(data)!=crc:raise RuntimeError('Invalid chip: '+name)
            dest=extracted/name
            if not dest.exists() or dest.read_bytes()!=data:dest.write_bytes(data)
            manifest.append({'file':name,'source':entry.filename,'bytes':size,'sha256':hashlib.sha256(data).hexdigest()})
    def run(*args,env=None):subprocess.run([str(x) for x in args],cwd=TC,env=env,check=True)
    run(sys.executable,'tools/make_prog.py','extracted')
    for kind,prefix,mode in [('sub','tc2sub','1'),('io','tc2io','0')]:
        output=TC/f'gen/tc2_{kind}.c'
        if not output.exists():
            env=os.environ.copy();env['PYTHONUTF8']='1'
            if kind=='io':env['H8_ROMSIZE']='0x4000'
            cov=[str(p.relative_to(TC).with_suffix('')) for p in sorted((TC/'cov').glob('h8'+kind+'_*.pcs'))]
            run(sys.executable,'tools/h8/h8_translate.py',kind+'.bin',mode,prefix,output,*cov,env=env)
    assets=ROOT/'build/assets/roms';assets.mkdir(parents=True,exist_ok=True)
    import shutil
    for m in manifest:shutil.copy2(extracted/m['file'],assets/m['file'])
    (assets.parent/'roms.sha256').write_text(''.join(m['sha256']+'  '+m['file']+'\n' for m in manifest),encoding='ascii')
    report={'upstream':PIN,'archive_sha256':hashlib.sha256(Path(rom).read_bytes()).hexdigest(),'chips':manifest}
    (ROOT/'build/rom-audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Validated {len(manifest)} arcade ROM chips; native MIPS and H8 sources ready.',flush=True)
    return assets.parent

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('rom',type=Path);a=p.parse_args();prepare(a.rom)
