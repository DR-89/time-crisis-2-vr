# Time Crisis II VR - Quickstart

Download **v0.2.0** from the [release page](https://github.com/DR-89/time-crisis-2-vr/releases/tag/v0.2.0). Both packages include the game files and animated weapons.

## Quest 3

1. Enable developer mode and accept USB debugging in the headset.
2. Sideload `TimeCrisis2VR-v0.2.0-quest3.apk` with SideQuest or `adb install -r TimeCrisis2VR-v0.2.0-quest3.apk`.
3. Open **Time Crisis II VR (Test)** from **Unknown sources**.
4. Wait for the arcade startup. Press **A** for credits, then either trigger to start.

## Windows PCVR

1. Extract the entire Windows ZIP to a writable folder.
2. Connect your headset through Virtual Desktop, Quest Link or your OpenXR setup. USB debugging alone is not a PCVR connection.
3. Run **Play VR.cmd**. Virtual Desktop on Quest 3 is the tested setup.
4. Use **Play Desktop.cmd** for mouse/keyboard play without a headset.

See [PCVR setup](PCVR.md) for runtime and troubleshooting details.

## Playing

- **Either trigger:** move the weapon to that hand and shoot.
- **Hold either grip:** leave cover. **Release both:** hide and reload.
- **A:** credits. **B:** laser on/off. **X:** recenter and calibrate upright height.
- **Left menu button:** options. In options, right stick click changes the default hand; right stick up/down changes **GUN ANGLE**; left stick click resets the angle.
- **Y in options:** switch physical ducking / grip cover. With physical ducking selected, calibrate upright using **X**, then duck to hide and stand back up to shoot.
- The arcade selects the machine gun automatically. Hold the trigger for automatic fire outside cover.

These face buttons assume the default right hand. The saved left-hand layout uses **X** for credits, **Y** for laser, **A** for recenter and **B** for cover-mode selection. Trigger handoffs do not change the saved face-button layout.

Laser assistance starts on; saved choices are retained. The release remains experimental and has not passed a complete-game playthrough. Full controls, test status and credits are in the [README](../README.md).
