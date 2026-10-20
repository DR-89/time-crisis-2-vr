# Time Crisis II VR v0.3.3

New since v0.3.0: **resolution presets**, **optional texture filtering**, and **Quest launcher artwork** for the standalone Quest and Windows OpenXR/desktop ports.

## Changes and controls

- Open options with the **left menu button**. Use **left stick up/down** to reach **GRAPHICS**.
- Select **Default**, **1280 x 1344**, **1680 x 1760**, **2550 x 2640**, **2940 x 3080**, or **3360 x 3520** pixels per eye with **right stick left/right**. **Left stick click** restores Default.
- Resolution saves automatically and applies after fully closing and restarting the app. The menu shows the active size separately from the saved selection. Default preserves the previous resolution; unsupported presets fall back to Default.
- **Texture filtering defaults to ON**, including older settings files. While paused, squeeze and release the **physical left grip** to toggle ON/OFF immediately. The setting is saved. Gameplay grip/cover controls are unchanged.
- Filtering smooths world textures while preserving HUD/text sampling. It does not add mipmaps or eliminate all distance shimmer.
- Android icons, adaptive icons and launcher banners replace missing/black artwork in launchers such as QGO.
- Windows keyboard: **Tab** changes the options page; **Left/Right** selects resolution on Graphics, **Home** restores Default, and **F** toggles filtering.

## Downloads

- **TimeCrisis2VR-v0.3.3-quest3.apk**: sideload onto Quest with `adb install -r`. Android version code **11**.
- **TimeCrisis2VR-v0.3.3-windows-x64.zip**: extract the entire archive. Connect an OpenXR headset and run **Play VR.cmd**, or use **Play SteamVR.cmd** to select an installed SteamVR runtime. **Play Desktop.cmd** starts mouse/keyboard play.
- **SHA256SUMS.txt**: checksums for the attached packages.

Game content, weapon models and attribution are included. Existing options and game data are retained by compatible in-place updates.

## Validation and limits

Settings migration/persistence, controller navigation and held-input guards, runtime/GPU size limits, and 38 GLES menu views pass in all three ports. Production world-filtering and overlay regressions pass. The Quest APK was installed and its hash verified; startup at **1680 x 1760** and **3360 x 3520** per eye was confirmed on Quest 3.

The Windows x64 package passes clean-ZIP extraction, internal hashes, model attribution and a native desktop render test through **1,501 simulation frames**, followed by normal shutdown with no CPU traps or graphics errors. This validates desktop startup/rendering, not a live PCVR headset session.

Higher resolutions require more GPU time and memory. Sustained performance for each preset and a new Windows headset playthrough remain unverified. These are experimental ports.

[README and controls](https://github.com/DR-89/time-crisis-2-vr/tree/v0.3.3#readme) · [Attribution](https://github.com/DR-89/time-crisis-2-vr/blob/v0.3.3/NOTICE.md)

Support development: [https://www.patreon.com/c/OpenSourceDevDirk](https://www.patreon.com/c/OpenSourceDevDirk)
