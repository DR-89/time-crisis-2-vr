"""Audit packaged launcher artwork and optional icon-only update (Pillow)."""
from pathlib import Path
from io import BytesIO
import argparse, json, subprocess, zipfile
from PIL import Image, ImageStat


def verify(apk, aapt, previous=None):
    badging=subprocess.check_output([str(aapt),'dump','badging',str(apk)],text=True)
    launch=next(s for s in badging.splitlines() if s.startswith('launchable-activity:'))
    assert "icon='res/mipmap-anydpi-v26/ic_launcher.xml'" in launch,launch
    tree=subprocess.check_output([str(aapt),'dump','xmltree',str(apk),'AndroidManifest.xml'],text=True)
    assert 'android:roundIcon(' in tree and tree.count('android:banner(')>=2
    checked=[]
    with zipfile.ZipFile(apk) as z:
        assert z.testzip() is None
        assert len(z.namelist())==len(set(z.namelist()))
        for density,size in [('mdpi',48),('hdpi',72),('xhdpi',96),('xxhdpi',144),('xxxhdpi',192)]:
            name=f'res/mipmap-{density}-v4/ic_launcher.png'
            im=Image.open(BytesIO(z.read(name))).convert('RGBA')
            assert im.size==(size,size) and im.getchannel('A').getextrema()==(255,255)
            assert max(ImageStat.Stat(im.convert('RGB')).stddev)>30,'Blank/dark icon'
            checked.append(size)
        for name,element in [('res/mipmap-anydpi-v26/ic_launcher.xml','adaptive-icon'),('res/drawable/launcher_foreground.xml','bitmap')]:
            xml=subprocess.check_output([str(aapt),'dump','xmltree',str(apk),name],text=True)
            assert element in xml and '@0x7f' in xml
        art=Image.open(BytesIO(z.read('res/drawable-nodpi-v4/launcher_art.png')))
        banner=Image.open(BytesIO(z.read('res/drawable-nodpi-v4/launcher_banner.png')))
        assert art.size==(512,512) and banner.size==(320,180)
        preserved=0
        if previous:
            with zipfile.ZipFile(previous) as old:
                names=[n for n in old.namelist() if n.startswith(('lib/','assets/roms/','assets/models/')) or n=='classes.dex']
                assert set(names)=={n for n in z.namelist() if n.startswith(('lib/','assets/roms/','assets/models/')) or n=='classes.dex'}
                for name in names:assert old.read(name)==z.read(name),name+' unexpectedly changed'
                preserved=len(names)
    result={'apk':str(apk),'legacy_icon_sizes':checked,'adaptive_icon':True,'banner':True,'unchanged_runtime_files':preserved}
    print(json.dumps(result,indent=2))
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('apk',type=Path);p.add_argument('--aapt',type=Path,required=True)
    p.add_argument('--previous',type=Path)
    a=p.parse_args();verify(a.apk,a.aapt,a.previous)
