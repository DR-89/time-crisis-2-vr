"""Resize existing launcher artwork into Android resources (requires Pillow).

Checked-in resources make Pillow unnecessary for ordinary APK builds.
Usage: python tools/prepare_launcher_icons.py quest/artwork/launcher.png
"""
from pathlib import Path
import argparse
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]


def prepare(source, banner=None):
    art = Image.open(source).convert('RGB')
    if art.width != art.height:
        raise ValueError('Launcher artwork must be square')
    resources = ROOT / 'quest/res'
    for density, size in [('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)]:
        target = resources / ('mipmap-' + density)
        target.mkdir(parents=True, exist_ok=True)
        art.resize((size, size), Image.Resampling.LANCZOS).save(target / 'ic_launcher.png')
    target = resources / 'drawable-nodpi'
    target.mkdir(parents=True, exist_ok=True)
    art.resize((512, 512), Image.Resampling.LANCZOS).save(target / 'launcher_art.png')
    # Fit preserves the complete existing cover/logo; never crop a game title.
    wide = ImageOps.pad(Image.open(banner).convert('RGB') if banner else art,
                        (320, 180), method=Image.Resampling.LANCZOS, color='#071326')
    wide.save(target / 'launcher_banner.png')
    target = resources / 'drawable'
    target.mkdir(exist_ok=True)
    (target / 'launcher_foreground.xml').write_text('''<?xml version="1.0" encoding="utf-8"?>
<inset xmlns:android="http://schemas.android.com/apk/res/android"
    android:insetLeft="19.4444%" android:insetTop="19.4444%"
    android:insetRight="19.4444%" android:insetBottom="19.4444%">
    <bitmap android:src="@drawable/launcher_art" android:gravity="fill" android:filter="true" />
</inset>
''', encoding='utf-8')
    target = resources / 'mipmap-anydpi-v26'
    target.mkdir(exist_ok=True)
    (target / 'ic_launcher.xml').write_text('''<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/launcher_background" />
    <foreground android:drawable="@drawable/launcher_foreground" />
</adaptive-icon>
''', encoding='utf-8')
    target = resources / 'values'
    target.mkdir(exist_ok=True)
    (target / 'launcher.xml').write_text('''<?xml version="1.0" encoding="utf-8"?>
<resources><color name="launcher_background">#071326</color></resources>
''', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--banner', type=Path)
    args = parser.parse_args()
    prepare(args.source, args.banner)
