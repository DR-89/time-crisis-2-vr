# Time Crisis II VR - Windows v0.3.0

**Render resolution:** open the menu with the **left menu button** (Escape on the keyboard) and pick **GRAPHICS**. Choose **Default**, **1280 x 1344**, **1680 x 1760**, **2550 x 2640**, **2940 x 3080** or **3360 x 3520** pixels per eye, matching the NFSU VR ports. Each change saves automatically and applies after **RESTART APP**; the running size is underlined. Default preserves the previous resolution (runtime recommendation capped at 1440 pixels on the longer side on Quest, 2160 on PCVR). Unsupported presets fall back to Default. Higher resolutions require more GPU time and memory; the highest setting is not a performance recommendation. Texture filtering stays enabled by default and still switches immediately.

Keyboard in the menu: the mouse points and clicks; arrows move and change, **Enter** selects, **Backspace** goes back, **Tab** / **Shift+Tab** switch tabs. Resolution presets affect OpenXR VR rendering; desktop mode continues to follow the window size.

**Texture filtering:** texture filtering defaults to ON and is saved in `quest-options.cfg`. Switch it on the menu's **GRAPHICS** tab; it applies immediately.

Extract the entire Windows ZIP to a writable folder. Keep `TimeCrisis2VR.exe`, the OpenXR loader, `models/` and `extracted/` together; do not rename the game files. No separate ROM import is required for the release package. A package built without game files (`tools/build_pc.py --no-roms`) has a `roms/` folder instead: put your own `timecrs2.zip` (US TSS3 Ver. B MAME set) there, and the first start checks it and unpacks it into `extracted/`.

## Start in VR

1. Connect your headset through Virtual Desktop, Quest Link or another OpenXR setup.
2. Make that software's OpenXR runtime active. The runtime must support OpenGL.
3. Run **Play VR.cmd** after the headset is connected.

**Virtual Desktop with Quest 3 was tested successfully.** On 2026-10-10 the runtime reported 90 Hz and 1980 x 2160 pixels per eye on an NVIDIA RTX 3050 Laptop GPU, and the user confirmed VR and controls. The app requests 120 Hz when the runtime offers it; it uses a supported lower rate otherwise. This short playtest does not establish stable frame delivery through every stage.

**Play SteamVR.cmd** selects a standard SteamVR installation for this launch only. Other runtimes and headsets remain unverified for this release. A USB/ADB connection by itself does not connect a PCVR headset.

## Controls

After the arcade startup, press **A** for credits and either trigger to start. Either trigger selects that hand and shoots. Hold either grip to leave cover; release both to hide/reload. **B** cycles LASER / OFF / TRACER, **X** recenters, and the left menu button opens the menu: default hand, aim guide, physical ducking, gun angle, gun fit, player route, world size, view distance, graphics and online play, plus RESUME, RESTART GAME, RECENTER and RESTART APP. Point the gun and pull the trigger, or use either stick with A / B; the grips switch tabs. Saved left-hand mode swaps the face-button layout: X credits, Y aim mode, A recenter.

The machine gun is equipped automatically in scripted sections. Hold the trigger outside cover for automatic fire. The tracked weapon changes model, and shots have recoil and muzzle flash; the Namco gun uses whole-model recoil. The older pistol remains a fallback.

**World scale:** on the menu's **PLAYER** tab, World size (25-400%) and View distance (0-100 cm). Start with 100% / 0 cm.

**Gun fit / player:** Gun height and Gun depth on the **GUN** tab; Player (Keith/P1 or Robert/P2) on the **PLAYER** tab, applied by RESTART APP. Robert uses the original alternate route in standalone mode and remains experimental, with the original local HUD/input slot.

L cycles the aim mode. Tracer Squeeze is a tracked-VR effect and flashes for 65 ms on accepted shots.

**Play Desktop.cmd** runs without a headset. Mouse aims, left mouse shoots, right mouse or Space operates the pedal, **5** inserts credits and **Escape** opens the menu, where the mouse points and clicks. Online, **M** mutes an open mic and holding **V** talks with push to talk (in VR too, beside the thumbstick clicks).

## Troubleshooting and diagnostics

- If startup fails, check `tc2.log`. `XR_ERROR_FORM_FACTOR_UNAVAILABLE` means the selected runtime has no available headset; connect it before relaunching.
- If you see only a desktop window, check the headset connection and active OpenXR runtime, and launch **Play VR.cmd**.
- Logs are `tc2.log` and `tc2-previous.log`; recorded inputs are `tc2_last.rec` and `tc2_prev.rec` beside the executable. Keep these after a crash.
- Input replays also need matching starting cabinet settings. Run `TimeCrisis2VR.exe --desktop --replay session.rec --frames 12000` for a bounded desktop replay.

v0.3.0 retains the transition-mask, HUD placement and sparse-input replay fixes from v0.2.0. New P1 and Robert-route replays each reach 28,201 frames with no traps, FP exceptions or unknown C435 commands. The following image comparisons refer to v0.2.0: A corrected 28,201-frame replay finished with zero CPU traps, floating-point exceptions or unknown C435 commands. Ninety-three desktop comparison frames are pixel-identical after the transition fix, and selected transitions passed both-eye shader checks. A complete-game playthrough is still pending.

Source, controls and attribution: [DR-89/time-crisis-2-vr](https://github.com/DR-89/time-crisis-2-vr). The arcade core comes from [spacestate1/namco22-decompile](https://github.com/spacestate1/namco22-decompile). See `NOTICE.md` and `licenses/` for license scope.

## Support development

If you enjoy this project and would like to support further development of my VR ports, you can support my work on Patreon:

[https://www.patreon.com/c/OpenSourceDevDirk](https://www.patreon.com/c/OpenSourceDevDirk)
