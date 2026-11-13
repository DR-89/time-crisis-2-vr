# Time Crisis II VR - Quickstart

**The menu:** the left menu button opens it. Point the gun at it and pull the trigger, or use either stick with **A** to select and **B** to go back; the grips switch tabs. **GRAPHICS** has the per-eye resolution (it applies after **RESTART APP**) and texture filtering (default ON).

Download **v0.4.0** from the [release page](https://github.com/DR-89/time-crisis-2-vr/releases/tag/v0.4.0). Both packages include the game files and animated weapons.

## Quest 3

1. Enable developer mode and accept USB debugging in the headset.
2. Sideload `TimeCrisis2VR-v0.4.0-quest3.apk` with SideQuest or `adb install -r TimeCrisis2VR-v0.4.0-quest3.apk`.
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
- **A:** credits. **B:** cycle LASER / OFF / TRACER. **X:** recenter and calibrate upright height.
- **Left menu button:** the menu. **GUN** holds the default hand, aim guide, gun angle and gun fit; **PLAYER** holds the player, cover mode, world size and view distance. Along the bottom: **RESUME**, **RESTART GAME** (back to the intro, coins kept), **RECENTER** and **RESTART APP**.
- **Physical ducking:** choose **DUCK** under **Take cover**, stand upright and press **RECENTER** (or **X**), then duck to hide and stand back up to shoot.
- The arcade selects the machine gun automatically. Hold the trigger for automatic fire outside cover.

These face buttons assume the default right hand. The saved left-hand layout uses **X** for credits, **Y** for aim mode and **A** for recenter. Trigger handoffs do not change the saved face-button layout.

**World size:** on the **PLAYER** tab, **World size** and **View distance**. Start with **100% / 0 cm** in a running level.

**Gun fit / Robert:** **Gun height** and **Gun depth** on the **GUN** tab move the gun in 5 mm steps; each has a reset button. **Player** on the **PLAYER** tab selects Keith or Robert; **RESTART APP** applies the route. Robert is an experimental standalone route, not linked multiplayer.

Laser assistance starts on; saved choices are retained. Choose TRACER for a brief orange shot line without a permanent laser. The release remains experimental and has not passed a complete-game playthrough. Full controls, test status and credits are in the [README](../README.md).
