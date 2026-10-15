# Time Crisis II VR

Play the **arcade version of Time Crisis II** in VR on **Meta Quest 3**, the **Valve Steam Frame** (standalone) or a **Windows PC with an OpenXR headset**, alone or with a second player on another headset over your local network. Aim with tracked controllers, duck into cover, and use the arcade's scripted machine-gun pickups with a matching animated weapon model.

Built on the native System 23 reconstruction in **[spacestate1/namco22-decompile](https://github.com/spacestate1/namco22-decompile)**, with the OpenXR host and controller features adapted from [Time Crisis VR](https://github.com/DR-89/time-crisis-vr). This port uses the arcade US TSS3 Ver. B game, not the PlayStation 2 version.

**[Download v0.3.3](https://github.com/DR-89/time-crisis-2-vr/releases/tag/v0.3.3)** | [Quickstart](docs/QUICKSTART.md) | [Steam Frame](docs/STEAM_FRAME_VR.md) | [LAN play](docs/LAN-PLAY.md) | [PCVR setup](docs/PCVR.md) | [Development](docs/DEVELOPMENT.md)

![Time Crisis II VR captured on Quest 3](docs/images/tc2-gameplay.jpg)

*Actual Quest 3 recording. The headset renders a separate tracked view for each eye.*

## Downloads and installation

| Platform | Download | Start playing |
| --- | --- | --- |
| Quest 3 standalone | [TimeCrisis2VR-v0.3.3-quest3.apk](https://github.com/DR-89/time-crisis-2-vr/releases/download/v0.3.3/TimeCrisis2VR-v0.3.3-quest3.apk) | Sideload with SideQuest or ADB, then open **Time Crisis II VR (Test)** under Unknown sources. |
| Windows x64 PCVR / desktop | [TimeCrisis2VR-v0.3.3-windows-x64.zip](https://github.com/DR-89/time-crisis-2-vr/releases/download/v0.3.3/TimeCrisis2VR-v0.3.3-windows-x64.zip) | Extract the entire ZIP. Connect your headset, then run **Play VR.cmd**. |
| Steam Frame standalone | Build it on a Mac (see [Build from source](#build-from-source)); the APK contains no game files | Install with the bundled installer, then launch **TimeCrisis2VR** from the Steam library. See [Steam Frame](#steam-frame). |

The release packages include the game files, Namco pistol, fallback pistol and machine-gun models, so no separate ZIP import is needed. The APK uses package `org.timecrisis2.arcadevr` and installs alongside the original Time Crisis VR. Updating an existing test build preserves local settings and cabinet data.

For ADB installation, enable Quest developer mode and accept USB debugging:

```powershell
adb install -r TimeCrisis2VR-v0.3.3-quest3.apk
```

For PCVR, connect the headset through **Virtual Desktop**, Quest Link, or another OpenXR setup **before** launching. An ADB cable connection alone is not a PCVR connection. The active runtime must support OpenGL. Virtual Desktop was tested with Quest 3; other runtimes remain unverified for this port. **Play SteamVR.cmd** selects a standard SteamVR installation for this app. **Play Desktop.cmd** runs without a headset.

After the arcade startup, press **A** for credits and then either **trigger** to start. Hold either **grip** to leave cover; release both to hide and reload.

## Steam Frame

The Steam Frame build runs standalone on the headset. SteamOS runs Android apps through Lepton, so this is the Quest app built for the Frame: the same game, options, physical cover and LAN play, with the Frame's own controller layout. Its APK contains no ROM. The installer copies your own `timecrs2.zip` to the Frame, and the game checks all 22 chips before using it.

1. Build the APK and installer on a Mac ([macOS steps below](#macos-steam-frame-and-quest)).
2. On the Frame, enable SSH in the developer settings and note its IP address.
3. Install it. The installer refuses to run while another Lepton app is open, backs up any earlier install and registers the Steam title:

   ```sh
   python3 artifacts/steam-frame/bundle/install.py --install --rom "$HOME/Downloads/timecrs2.zip" --host steamos@FRAME_IP_ADDRESS
   ```

   Add `--identity ~/path/to/devkit_rsa` if your Frame only accepts the SteamOS Devkit Client's key.
4. Launch **TimeCrisis2VR** from the Steam library. SteamVR only gives controller input to apps started from Steam, not from SSH.

Settings, logs and the imported ROM live in `~/Documents/TimeCrisis2VR` on the Frame (`/sdcard/Documents/TimeCrisis2VR` inside Lepton), so they survive reinstalls.

| Action | Steam Frame controllers |
| --- | --- |
| Select weapon hand / shoot | Either trigger |
| Leave cover / cover and reload | Hold either grip / release both |
| Insert credits | Right A |
| Cycle LASER / OFF / TRACER | Right B |
| Open / close options | Right Menu |
| Back | View (tap) |
| Recenter / recenter and calibrate upright height | Hold View 1 s / 3 s |
| Switch physical ducking / grip cover, in options | Right X |
| Options sticks, page changes and resets | Same as Quest (below) |

Rendering defaults to up to 2160 x 2160 per eye (the panel's native size) at the refresh rate set in SteamVR; the **GRAPHICS** resolution presets also apply. See [Steam Frame](docs/STEAM_FRAME_VR.md) for details and status.

## LAN multiplayer (two headsets)

Two players can play a linked game the way two arcade cabinets do: each player has their own headset and their own cabinet view, and the two copies of the game talk over your local network. Any mix of Quest 3, Steam Frame and Windows PC works in principle; Steam Frame hosting with a Quest 3 joining has been played.

**Before you start**

- Both headsets need the **same build** of the game. The lobby checks this and refuses mismatched versions.
- Both must be on the **same local network**. Discovery covers the host's /24 (for example `192.168.1.x`). On a larger network, use **Join by IP**.
- The host listens on **port 32123**, TCP and UDP. On Windows, allow the app through the firewall when asked.
- **Keep both headsets awake.** A Quest that is taken off goes to sleep and pauses its game, and the other player gets "Partner disconnected". If one person is testing both headsets, keep the Quest awake with developer tools (`adb shell am broadcast -a com.oculus.vrpowermanager.prox_close`; undo with `...automation_disable` or a reboot).

**Playing**

1. Both players: open options and move the **left stick up** through the pages to **LAN PLAY**: Options, Gun Fit / Player, Graphics, then LAN Play.
2. Player 1 chooses **Host**. The screen shows the host's IP address. The host is the red cabinet (Keith).
3. Player 2 chooses **Find a Game** and picks the host, or **Join by IP** and enters the host's address. The joiner is the blue cabinet (Robert). On the Frame the SteamVR keyboard opens, and on Quest the system keyboard.
4. Both select **Ready**; the host selects **Start**. Both cabinets restart and boot as a linked pair, which takes about 20 seconds of the arcade's power-on test. Then insert credits and start as usual.
5. **Leave** in the lobby returns to normal solo play. Linked games keep their own saved cabinet data, so your solo settings and scores are untouched.

While linked, opening options doesn't pause the game: your input is ignored, but the arcade keeps running for the other player.

| Problem | What to try |
| --- | --- |
| Find a Game shows nothing | Check both are on the same network and subnet, then use Join by IP with the address on the host's screen |
| "Game/version mismatch - update both" | Install the same build on both headsets |
| "Partner disconnected" / "Connection timed out" | The other headset went to sleep, lost Wi-Fi or quit; both choose Leave and start again |
| Host can't open the port | Close other copies of the game; on Windows, allow it through the firewall |

More detail: [LAN play](docs/LAN-PLAY.md).

## Features

New in **v0.3.3**: saved per-eye resolution presets, default-on texture filtering with an immediate toggle, and Quest launcher icons/banners. See the [release notes](docs/RELEASE_NOTES_0.3.3.md).

- Native arcade MIPS and H8 code translated to C, with the original textured world rendered from tracked stereo viewpoints.
- Standalone Quest and Windows PCVR builds, plus a normal desktop mode, and a standalone [Steam Frame](docs/STEAM_FRAME_VR.md) build (ROM-free, built from source).
- [LAN play](docs/LAN-PLAY.md): two cabinets linked over the network, as in the arcade, between any of the headsets.
- Shoot with either hand: pressing a trigger moves the weapon to that controller.
- Grip-based cover or physical cover: lean or duck the way a SAFE sign points, with upright-height calibration.
- Adjustable gun pitch from **-60 to +60 degrees**, plus saved **WORLD SIZE (25–400%)** and **VIEW DISTANCE (0–100 cm)**.
- Aim assistance cycles through **LASER / OFF / TRACER**. Tracer Squeeze flashes
  an orange ray for 65 ms on an accepted shot, using the existing surface hit.
- Martoscar's Namco Arcade Gun, with saved vertical/depth grip offsets.
- Select Keith's P1 or Robert's P2 route for the next app launch (experimental).
- Automatic pistol / Tripo3D machine-gun model changes using the arcade's actual weapon state.
- Whole-model recoil for the Namco gun, muzzle flash and controller vibration; the older fallback pistol retains its slide animation.
- HUD health, ammunition and player markers aligned on the same plane.
- Cinematic black bars removed in VR while retaining dialogue and transition text.
- Correct sRGB handling for arcade colors and refresh-rate selection from the runtime's supported rates.

## Controls

**Render resolution:** open options with the **left menu button**, then use **left stick up/down** to find **GRAPHICS**. Move the **right stick left/right** to choose **Default**, **1280 x 1344**, **1680 x 1760**, **2550 x 2640**, **2940 x 3080** or **3360 x 3520** pixels per eye, matching the NFSU VR ports. Click the **left stick** to restore Default. Each change saves automatically; **fully close and restart the app** to apply it. The panel shows the active size and a restart reminder. Default preserves the previous resolution (runtime recommendation capped at 1440 pixels on the longer side on Quest, 2160 on PCVR and Steam Frame). Unsupported presets fall back to Default. Higher resolutions require more GPU time and memory; the highest setting is not a performance recommendation. Texture filtering stays enabled by default and still switches immediately.

**Texture filtering:** open the VR options with the **left menu button**, then squeeze and release the **left grip** to switch **TEXTURE FILTERING: ON / OFF**. It defaults to **ON**, including when upgrading an older settings file, and saves automatically. Changes apply immediately; no restart is needed. This control uses the physical left controller in either handedness layout. During gameplay, grips still operate cover. Filtering smooths world textures; HUD and text retain their existing sampling.

The default hand determines the face-button layout. Trigger handoffs do not change that saved layout.

| Action | Default hand: right | Default hand: left |
| --- | --- | --- |
| Select weapon hand / shoot | Either trigger | Either trigger |
| Leave cover | Hold either grip | Hold either grip |
| Cover / reload | Release both grips | Release both grips |
| Insert credits | A, right controller | X, left controller |
| Cycle LASER / OFF / TRACER | B, right controller | Y, left controller |
| Recenter / calibrate upright height | X, left controller | A, right controller |
| Open / close options | Left menu button | Left menu button |
| Change default hand, in options | Right stick click | Right stick click |
| Switch physical ducking / grip cover, in options | Y, left controller | B, right controller |
| Adjust world size, in options | Right stick left / right | Right stick left / right |
| Adjust gun angle, in options | Right stick up / down | Right stick up / down |
| Reset gun angle, in options | Left stick click | Left stick click |
| Switch options page | Left stick up / down | Left stick up / down |
| Gun fit page: move gun up / down | Right stick up / down | Right stick up / down |
| Gun fit page: move gun back / forward | Right stick right / left | Right stick right / left |
| Gun fit page: reset both offsets | Left stick click | Left stick click |
| Gun fit page: select P1 / P2 for next launch | Left stick left / right | Left stick left / right |

**World size:** open the left menu and move the **right stick left/right** to
adjust **WORLD SIZE** from **25% to 400%** in 5-point steps. Hold to repeat.
Higher values make the world larger. The new **100%** starting value is calibrated
against complete figures: Robert is about 1.70 m tall and two standing guards are
about 1.89 m in the measured opening scene. This replaces the provisional 200%
default. Existing saved percentages are retained; select 100% to try the new
calibration. The setting is saved automatically. Windows also accepts Left/Right;
End restores 100%.

**View distance:** move the **left stick left/right** in options to adjust
**VIEW DISTANCE** from 0 to 100 cm in 5 cm steps (default **0 cm**). This adds
space between the camera and the world; it makes nearby cover appear smaller
without changing figure height. Windows also accepts Page Up/Page Down.
Head movement, controller/weapon size, HUD/menu size and physical ducking
distances stay in real metres. Target rays follow the rendered surfaces after
either setting changes, including while paused. The calibration and settings
need a Quest playtest to confirm the perceived size and comfort.

**Physical cover:** select **PHYSICAL DUCKING** as the cover mode, stand or sit upright and use the recenter button. At each cover spot a **SAFE** sign shows which way the arcade hides you: lean or duck about 35 cm that way to hide/reload, then come back upright to leave cover. Where the arcade does not supply a cover spot, ducking 20 cm hides you as before. If the gun points too high, reduce **GUN ANGLE** in options.

**New in v0.3.3:** Tracer Squeeze, the Namco gun, grip-fit controls and experimental Robert-route selection accompany the calibrated world-size fix.
On **GUN FIT / PLAYER**, positive **GUN UP** raises the gun and positive
**GUN BACK** pulls it towards your hand, in 5 mm steps up to 200 mm each way.
The offsets follow the controller and affect both the visible model and muzzle;
world size does not change them. Their zero point uses the new model's upper
grip; fine alignment with your controller or shell needs a headset check.
**PLAYER: P2 ROBERT BAXTER** selects the original Robert route on the next app
launch. Completely close and reopen the app after changing it. This is standalone
play; the core still uses its local input/HUD slot and does not emulate a linked
second cabinet. See [implementation and validation details](docs/FEEDBACK-TEST.md).

The machine gun is equipped automatically during scripted sections, including Stage 1, Area 2. **Hold the trigger for automatic fire** while out of cover. There is no separate weapon-selection button. See [weapon details](docs/WEAPONS.md).

Desktop controls: mouse to aim, left mouse to shoot, right mouse or Space for cover/pedal, **5** for credits, and **Escape** for options.

## Tested status and limitations

**v0.3.3 is experimental.** It adds resolution presets, optional texture filtering and Quest launcher artwork. The earlier v0.3.0 release introduced the calibrated world-scale and `0.2.0-feedback-test.1` changes. The Namco model and options were exercised on Quest 3; the runtime confirmed 120 Hz during the test startup. Both P1 and Robert-route replays completed 28,201 frames with zero CPU traps, floating-point exceptions or unknown C435 commands. Neither a full Robert-route playthrough nor sustained 120 FPS is established.

- **Quest 3:** immersive VR and gameplay tested. Health/ammunition alignment and automatic machine-gun switching were confirmed in headset. The runtime has confirmed 120 Hz in standalone sessions; short samples do not establish uninterrupted 120 FPS throughout the game or thermal stability.
- **Steam Frame:** start-up, gameplay, physical cover with the SAFE sign, and a LAN game with the Frame hosting and a Quest 3 joining were played on a build of this work before its rebase onto v0.3.x. The v0.3.x menus on the Frame, clean shutdown, and the SteamVR keyboard haven't been played yet. See [Steam Frame](docs/STEAM_FRAME_VR.md).
- **LAN play:** Frame-to-Quest played for about three minutes linked; a two-core simulation plays two linked games and returns to solo. Windows LAN play is untested. See [LAN play](docs/LAN-PLAY.md).
- **Windows PCVR:** the earlier v0.2.0 was tested on Quest 3 through Virtual Desktop on 2026-10-10. The user confirmed VR and controls work and reported a successful playtest. The runtime confirmed **90 Hz**, **1980 x 2160 per eye**, on an **NVIDIA RTX 3050 Laptop GPU**. This is a short gameplay check, not a full-game performance benchmark.
- **Replay and graphics checks:** a corrected 28,201-frame replay completed with zero CPU traps, floating-point exceptions or unknown C435 commands. All 93 desktop comparison frames were pixel-identical after the transition fix; selected VR transition frames passed both-eye rendering checks. Those transition comparisons are from v0.2.0; see [validation details](docs/TRANSITIONS.md). New tests cover 21 menu variants, tracer lifetime/stereo, weapon rendering, grip offsets and paused scale changes.
- Quest 2/3S, other PC headsets and other OpenXR runtimes have not been confirmed for this release. Later stages, all effects, audio behavior and sustained performance need broader testing. Some rendering differences may remain.

The arcade simulation runs at approximately **59.906 Hz**. Headset presentation and tracking run at the selected headset rate. The app requests 120 Hz when offered, otherwise a supported lower rate; the Virtual Desktop test offered 90 Hz.

## Reporting problems

Open an [issue](https://github.com/DR-89/time-crisis-2-vr/issues) with the version, headset/runtime, stage/area and what happened. For a crash, preserve the logs before repeatedly relaunching.

- **Quest:** connect ADB and run `Export-Quest-Diagnostics.ps1` from this repository. It saves current/previous game logs, recorded inputs, settings and Android exit information locally. Review the files before attaching them to an issue.
- **Windows:** logs are `tc2.log` / `tc2-previous.log`; inputs are `tc2_last.rec` / `tc2_prev.rec` beside the executable.

Input recordings contain arcade controls, not video or headset poses. They do not embed the starting cabinet backup RAM, so an exact replay also needs matching cabinet settings. Reports are not uploaded automatically.

## Build from source

You need your own **Time Crisis II US TSS3 Ver. B** arcade ROM set, `timecrs2.zip`. The build checks all 22 chips against the pinned table. ROMs, signing keys and build output never go into Git.

Clone the repository with its `upstream` submodule:

```sh
git clone --recurse-submodules https://github.com/DR-89/time-crisis-2-vr.git
cd time-crisis-2-vr
```

### Windows: PCVR and Quest

Install Git, Python 3 and CMake. For Quest, also install a JDK, and Android SDK platform 35, build-tools 35.0.1 and NDK 28.2.13676358, with the SDK at `.tools/android-sdk`. The scripts fetch their pinned SDL, OpenXR, Ninja and Windows compiler dependencies.

```powershell
python -X utf8 tools/build_pc.py --rom "$env:USERPROFILE\Downloads\timecrs2.zip" --jobs 2
python -X utf8 tools/build.py --rom "$env:USERPROFILE\Downloads\timecrs2.zip" --bundle-roms --jobs 2
```

Set `JAVA_HOME` to your JDK before building Android. See [development instructions](docs/DEVELOPMENT.md) for SDK setup, tests and output paths.

### macOS: Steam Frame and Quest

The Steam Frame build is made on a Mac (Apple Silicon or Intel); the same script also builds the Quest APK.

1. **Install the tools** (once):

   ```sh
   xcode-select --install
   brew install cmake ninja python openjdk@17
   brew install --cask android-commandlinetools
   export JAVA_HOME="$(brew --prefix openjdk@17)/libexec/openjdk.jdk/Contents/Home"
   yes | sdkmanager --sdk_root="$HOME/Library/Android/sdk" --licenses
   sdkmanager --sdk_root="$HOME/Library/Android/sdk" "platforms;android-34" "build-tools;35.0.0" "ndk;27.2.12479018"
   ```

   Android Studio's SDK Manager works too: install those same three packages. The build looks for the SDK in `~/Library/Android/sdk` (or `ANDROID_SDK_ROOT`), and for JDK 17 in `JAVA_HOME`, Android Studio or Homebrew.

2. **Fetch the pinned SDL 2.30.11 source and OpenXR 1.1.43 loader** into `.tools/` (checked by SHA-256):

   ```sh
   python3 tools/bootstrap_frame.py
   ```

3. **Build the Steam Frame APK and its installer bundle:**

   ```sh
   python3 tools/build_frame.py --rom "$HOME/Downloads/timecrs2.zip" --jobs 4
   python3 tools/steam_frame/make_bundle.py
   ```

   This writes `artifacts/steam-frame/TimeCrisis2VR-SteamFrame-arm64.apk` and the ROM-free installer in `artifacts/steam-frame/bundle/`. The ROM is used only to generate the native code and the chip checksums; it isn't packaged. Install it as described under [Steam Frame](#steam-frame).

4. **Build the Quest APK** (optional):

   ```sh
   python3 tools/build_frame.py --flavor quest --rom "$HOME/Downloads/timecrs2.zip" --jobs 4
   adb install -r artifacts/quest/TimeCrisis2VR-Quest-arm64.apk
   ```

   This is ROM-free by default: on first launch, a setup screen imports your `timecrs2.zip`. Add `--bundle-roms` for a private APK with your chips inside (`TimeCrisis2VR-Quest-arm64-with-ROM.apk`). Never share that one.

Signing keys are created on the first build in `~/.config/timecrisis2vr/` and reused afterwards. Keep them: an update signed with a different key can't install over an existing copy without uninstalling it first, and uninstalling deletes its saved settings.

To run the tests that work on a Mac (after a Steam Frame build, whose configure step they reuse):

```sh
export TCVR_SHARED_TOOLS="$PWD/.tools"
for t in test_frame_profile test_cover_spot test_tc2_session test_tc2_link test_quest_lobby test_openvr_keyboard test_frame_storage test_frame_bundle test_frame_manifest test_tc2_frame_shutdown; do python3 tests/$t.py || break; done
python3 tests/test_frame_profile.py --touch
```

## Support development

If you enjoy this project and would like to support further development of my VR ports, you can support my work on Patreon:

[https://www.patreon.com/c/OpenSourceDevDirk](https://www.patreon.com/c/OpenSourceDevDirk)

## Credits and licensing

- **[spacestate1/namco22-decompile](https://github.com/spacestate1/namco22-decompile):** the System 23 reconstruction and shared engine that make this port possible. This release pins its `tc2/` core at [`0837a397`](https://github.com/spacestate1/namco22-decompile/tree/0837a397ebe554dbf5341d95629aa1e29fc58a52/tc2).
- **[DR-89/time-crisis-vr](https://github.com/DR-89/time-crisis-vr/tree/v0.8.4):** the original port's OpenXR host, controller options and weapon rendering.
- **FireWarden:** CC0 [Lowpoly Pistol](https://opengameart.org/content/lowpoly-pistol), imported with separate animated parts.
- **Martoscar:** [Namco Arcade Gun](https://sketchfab.com/3d-models/namco-arcade-gun-15fbd5b9add94a34b5c21746e3dd32be),
  CC BY 4.0; converted with texture baking, grip calibration and recoil for this port.
- **Tripo3D:** user-supplied machine-gun model, optimized for this port.
- **[astromaddie](https://github.com/astromaddie):** Steam Frame build, LAN play and move-to-hide physical cover.
- SDL2, Khronos OpenXR, zlib and other dependencies retain their original licenses.

The port code is MIT licensed. **Original Time Crisis II game code, graphics, sound, ROM contents and trademarks retain their original rights and are not covered by the MIT license.** This is an unofficial project, unaffiliated with Bandai Namco or Meta. See [NOTICE.md](NOTICE.md) for attribution and license scope.

Inherited TC1-specific files and historical documents are reference material, not TCII validation results. The active game source list is `quest/tc2_sources.cmake`.
