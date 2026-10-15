# Time Crisis II VR - Quickstart

Download **v0.3.0** from the [release page](https://github.com/DR-89/time-crisis-2-vr/releases/tag/v0.3.0). Both packages include the game files and animated weapons. Steam Frame builds are separate and ROM-free; see below.

## Quest 3

1. Enable developer mode and accept USB debugging in the headset.
2. Sideload `TimeCrisis2VR-v0.3.0-quest3.apk` with SideQuest or `adb install -r TimeCrisis2VR-v0.3.0-quest3.apk`.
3. Open **Time Crisis II VR (Test)** from **Unknown sources**.
4. Wait for the arcade startup. Press **A** for credits, then either trigger to start.

## Windows PCVR

1. Extract the entire Windows ZIP to a writable folder.
2. Connect your headset through Virtual Desktop, Quest Link or your OpenXR setup. USB debugging alone is not a PCVR connection.
3. Run **Play VR.cmd**. Virtual Desktop on Quest 3 is the tested setup.
4. Use **Play Desktop.cmd** for mouse/keyboard play without a headset.

See [PCVR setup](PCVR.md) for runtime and troubleshooting details.

## Steam Frame (macOS build)

The Steam Frame flavor is an ARM64 Android/OpenXR developer build with package ID `org.timecrisis2.frame`. Its APK and installer bundle are ROM-free; on first launch, select your own `timecrs2.zip` or copy it to `/sdcard/Documents/TimeCrisis2VR/timecrs2.zip`. The launcher verifies all 22 required chips by hash and stores the archive, extracted chips, settings, logs, and captures in `Documents/TimeCrisis2VR`.

Build on macOS with the pinned Android SDK/NDK and the already verified shared dependency cache:

```sh
python3 tools/build_frame.py --rom "$HOME/Downloads/timecrs2.zip" --shared-tools /path/to/time-crisis-vr/.tools --jobs 2
```

The APK is written to `artifacts/steam-frame/TimeCrisis2VR-SteamFrame-arm64.apk`; `artifacts/steam-frame/bundle` contains the checksummed ROM-free installer. The bundle defaults to local verification or a non-writing dry run. Installation requires an explicit `--install`, a matching Steam Frame booking lock script, and the user's ROM path; see [Steam Frame verification](VR_ARCHITECTURE.md).

With the Steam Frame controller profile active, either trigger fires, either grip leaves cover, right A adds credits, right B toggles the laser, right Menu opens/resumes options, and right X changes cover mode in options. Left View taps back/resume, holds for one second to recenter, or holds for three seconds to recenter and calibrate upright height. OpenXR Touch remains available as a fallback.

## Playing

- **Either trigger:** move the weapon to that hand and shoot.
- **Hold either grip:** leave cover. **Release both:** hide and reload.
- **A:** credits. **B:** cycle LASER / OFF / TRACER. **X:** recenter and calibrate upright height.
- **Left menu button:** options. In options, right stick click changes the default hand; right stick up/down changes **GUN ANGLE**; left stick click resets the angle.
- **Y in options:** switch physical ducking / grip cover. With physical ducking selected, calibrate upright using **X**, then lean or duck the way the **SAFE** sign points to hide, and come back upright to shoot.
- The arcade selects the machine gun automatically. Hold the trigger for automatic fire outside cover.

These face buttons assume the default right hand. The saved left-hand layout uses **X** for credits, **Y** for aim mode, **A** for recenter and **B** for cover-mode selection. Trigger handoffs do not change the saved face-button layout.

**World size:** in options, right stick left/right adjusts WORLD SIZE; left stick left/right adjusts VIEW DISTANCE. Start with **100% / 0 cm** in a running level.

**Gun fit / Robert:** left stick up/down switches to GUN FIT / PLAYER. Right stick up/down adjusts gun height and right/left pulls it back/forward in 5 mm steps. Left click resets both offsets. Left stick left/right selects Keith or Robert; fully restart the app to apply the route. Robert is an experimental standalone route, not linked multiplayer.

Laser assistance starts on; saved choices are retained. Choose TRACER for a brief orange shot line without a permanent laser. The release remains experimental and has not passed a complete-game playthrough. Full controls, test status and credits are in the [README](../README.md).
