# Launcher artwork

`launcher.png` is the source artwork; checked-in Android resources are generated
by `python tools/prepare_launcher_icons.py quest/artwork/launcher.png` (Pillow).
Time Crisis II also passes `--banner quest/artwork/banner.png`.

The application declares its icon, round icon and banner. Legacy PNG icons cover
mdpi through xxxhdpi; the adaptive foreground fits inside the 66/108 safe region.
The standard APK build compiles these resources with AAPT2; no image library is
needed to build an APK. Launcher activities explicitly reference the artwork.

Game names and original artwork remain their respective owners' property.
