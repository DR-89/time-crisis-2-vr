# Time Crisis II VR

A local Quest 3 and Windows PCVR port of the **arcade version** of Time Crisis II (US, TSS3 Ver. B).

Based on the native System 23 game core in [spacestate1/namco22-decompile](https://github.com/spacestate1/namco22-decompile/tree/0837a397ebe554dbf5341d95629aa1e29fc58a52/tc2), with the OpenXR host and controller features adapted from [Time Crisis VR v0.8.4](https://github.com/DR-89/time-crisis-vr/tree/v0.8.4).

This is a separate app: Android package `org.timecrisis2.arcadevr`. It does not replace Time Crisis VR or the separate PS2 research project.

## Development status

Local test builds are available for both platforms. The Quest 3 build boots directly in immersive VR, renders separate eye views and has reached gameplay with a tracked pistol. The first headset log confirms 120 Hz; a short 160-second runtime sample reported at least 119 submitted frames/s in 159 samples. These samples include capture stalls and focus changes and do not establish full-game or thermal stability. Windows desktop and spatial-renderer replays reached gameplay and shut down normally with zero processor traps. PCVR headset validation is still pending. No release has been published.

Test build 2 also keeps the health and ammunition models on the HUD plane, so head translation does not separate them from their frames. Full-stage compatibility, stereo scale, aiming accuracy, audio and sustained performance still need broader gameplay testing.

On 2026-10-10, the first Quest play session exited normally at frame 18,277 with zero CPU traps, zero floating-point exceptions and no unknown C435 commands. A Windows spatial-renderer run using the captured input sequence also exited normally at frame 18,270. The HUD-corrected test build 2 is installed, but its final visual check in the headset is pending. The PCVR headset test was deferred by the user.

The original MIPS game program and both H8 programs run as translated native C. The GPU draws the original textured polygons from a separate tracked viewpoint for each eye. The arcade text layer and direct screen graphics remain on a camera-aligned plane. The original arcade simulation runs at its original rate; OpenXR presentation and controller tracking use the headset rate.

## Controls

| Action | Control |
| --- | --- |
| Select weapon hand and shoot | Left or right trigger |
| Leave cover | Hold either grip |
| Cover / reload | Release both grips |
| Insert coins | A with default right hand; X with default left hand |
| Toggle laser | B with default right hand; Y with default left hand |
| Recenter and calibrate upright height | X with default right hand; A with default left hand |
| Open / close options | Left menu button |
| Default weapon hand, in options | Right stick click |
| Physical ducking / grip cover, in options | Y with default right hand; B with default left hand |
| Gun angle, in options | Right stick up / down, -60 to +60 degrees |
| Reset gun angle, in options | Left stick click |

Laser is enabled by default. Options are saved locally. Trigger hand switching does not change the saved face-button layout. All player-facing text is English.

## Build locally

Required: CMake, Python 3, the pinned SDL/OpenXR/LLVM-MinGW dependencies fetched by the build scripts, and an Android SDK with platform 35, build-tools 35.0.1 and NDK 28.2.13676358 at `.tools/android-sdk`. Set `JAVA_HOME` to a JDK. This development checkout reuses the user's existing Android SDK through a local junction.

```powershell
python -X utf8 tools/prepare_tc2.py C:\Users\Dirk\Downloads\timecrs2.zip
python -X utf8 tools/build_pc.py --skip-prepare --jobs 2
python -X utf8 tools/build.py --skip-prepare --bundle-roms --rom C:\Users\Dirk\Downloads\timecrs2.zip --jobs 2
```

The ROM preparer checks the 22 chip sizes and CRCs against the pinned upstream table. It accepts the older `tss3verb.3` filename when its content matches `tss1vera.3`. SHA-256 checksums are recorded in `build/rom-audit.json`; the original ZIP is never modified. Game data is kept in ignored local build/output directories. The local test APK includes the supplied ROM data for direct launch.

Outputs:

- `artifacts/bundled/TimeCrisis2VR-with-ROM.apk`
- `artifacts/pc/TimeCrisis2VR-PC/TimeCrisis2VR.exe`
- `artifacts/pc/TimeCrisis2VR-v0.1.0-windows-x64.zip`

## Testing

`tests/test_handedness.py` exercises the real shared input code with a mock OpenXR runtime: both hands, trigger handoff, pitch, saved options and focus/tracking loss. `tests/tc2_scene_fixture.c` checks the System 23 projection, camera offsets, surface aiming and fallback plane. `tests/verify_apk.py` checks the packaged chips, ARM64 libraries and immersive Android entry point.

Use `Export-Quest-Diagnostics.ps1` to save a local report with the game logs, `tc2_last.rec` / `tc2_prev.rec` input recordings and VR options. Nothing is uploaded. On Windows, `--desktop --replay session.rec --frames 12000` runs a bounded replay. The upstream recording stores inputs; it does not embed the starting backup RAM, so reproducing a session also requires matching cabinet settings.

The inherited TC1-specific source files, tests and historical documentation are reference material; they are not evidence that the System 23 port has passed those checks. The active core source list is `quest/tc2_sources.cmake`.

## Credits and licensing

The upstream System 23 reconstruction is by **spacestate1** and contributors. Their own code is MIT licensed; the translated original game program and all ROM content retain their original rights. Time Crisis II and its game assets belong to Namco / Bandai Namco. SDL2, OpenXR and the CC0 pistol model retain their respective licenses. See `NOTICE.md` and the original upstream license.
