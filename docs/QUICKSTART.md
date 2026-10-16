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

## Steam Frame

The Steam Frame build is a standalone ARM64 Android/OpenXR app that SteamOS runs through Lepton, package `org.timecrisis2.frame`. The APK and installer bundle contain no ROM: the installer copies your own `timecrs2.zip` to the Frame, and the app checks all 22 chips by SHA-256 before using it. There is no prebuilt release yet; build it on macOS:

```sh
python3 tools/bootstrap_frame.py
python3 tools/build_frame.py --rom "$HOME/Downloads/timecrs2.zip" --jobs 4
python3 tools/steam_frame/make_bundle.py
python3 artifacts/steam-frame/bundle/install.py --install --rom "$HOME/Downloads/timecrs2.zip" --host steamos@YOUR_FRAME_ADDRESS
```

Then launch **TimeCrisis2VR** from the Steam library. See [Steam Frame](STEAM_FRAME_VR.md) for requirements, controls and status.

## Playing

- **Either trigger:** move the weapon to that hand and shoot.
- **Hold either grip:** leave cover. **Release both:** hide and reload.
- **A:** credits. **B:** cycle LASER / OFF / TRACER. **X:** recenter and calibrate upright height.
- **Left menu button:** options. In options, right stick click changes the default hand; right stick up/down changes **GUN ANGLE**; left stick click resets the angle.
- **Y in options:** switch physical ducking / grip cover. With physical ducking selected, calibrate upright using **X**, then lean or duck the way the **SAFE** sign points to hide, and come back upright to shoot.
- The arcade selects the machine gun automatically. Hold the trigger for automatic fire outside cover.
- **LAN play:** in options, push the left stick up twice to **LAN PLAY**. One player hosts, the other uses **Find a Game** (or **Join by IP**); both select Ready and the host starts. See [LAN play](LAN-PLAY.md).

These face buttons assume the default right hand. The saved left-hand layout uses **X** for credits, **Y** for aim mode, **A** for recenter and **B** for cover-mode selection. Trigger handoffs do not change the saved face-button layout.

**World size:** in options, right stick left/right adjusts WORLD SIZE; left stick left/right adjusts VIEW DISTANCE. Start with **100% / 0 cm** in a running level.

**Gun fit / Robert:** left stick up/down switches to GUN FIT / PLAYER. Right stick up/down adjusts gun height and right/left pulls it back/forward in 5 mm steps. Left click resets both offsets. Left stick left/right selects Keith or Robert; fully restart the app to apply the route. Robert is an experimental standalone route, not linked multiplayer.

Laser assistance starts on; saved choices are retained. Choose TRACER for a brief orange shot line without a permanent laser. The release remains experimental and has not passed a complete-game playthrough. Full controls, test status and credits are in the [README](../README.md).
