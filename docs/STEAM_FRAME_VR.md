# Steam Frame

A standalone build for the Valve Steam Frame: no PC involved. The Frame runs Android apps through Lepton, Steam's Android compatibility layer, so this is the existing Quest host built as an `arm64-v8a` Android/OpenXR APK with `TCVR_STEAM_FRAME` defined, installed as a Steam devkit title that uses the Lepton compatibility tool. The game, renderer and options are the same code as the Quest build.

## Stack

- The Quest host in `quest/`, compiled with `TCVR_STEAM_FRAME` (package `org.timecrisis2.frame`, Android API 29, GLES 3).
- SDL 2.30.11 from source, with a Frame-only Java patch (below).
- The Khronos OpenXR loader for Android 1.1.43, talking to SteamVR's OpenXR runtime. Lepton maps GLES onto Zink and Turnip on the Frame's Adreno 750.
- No ROM in the APK. The launcher (`FrameStorage`) imports the player's `timecrs2.zip` into `/sdcard/Documents/TimeCrisis2VR`, which is `~/Documents/TimeCrisis2VR` on SteamOS and survives reinstalls, and checks all 22 chips by SHA-256.

## Build, install, launch

On macOS, x86_64 Linux or Windows through WSL2, with JDK 17, CMake, Ninja and the Android SDK (platform 34, build-tools 35.0.0, NDK 27.2.12479018); the README has the setup commands for each:

```sh
python3 tools/bootstrap_frame.py           # SDL 2.30.11 + OpenXR 1.1.43 loader into .tools, hash-checked
python3 tools/build_frame.py --rom "$HOME/Downloads/timecrs2.zip" --jobs 4
python3 tools/steam_frame/make_bundle.py   # checksummed, ROM-free installer bundle
```

The ROM is only used at build time to generate the native H8 programs and the chip-hash manifest; it is not packaged. To install, enable SSH on the Frame and run:

```sh
python3 artifacts/steam-frame/bundle/install.py --install --rom "$HOME/Downloads/timecrs2.zip" --host steamos@YOUR_FRAME_ADDRESS
```

Add `--identity` if the Frame only accepts a particular key, such as the SteamOS Devkit Client's `devkit_rsa`. The installer refuses to run while a Lepton app is active. It backs up and verifies any existing title and data, copies the APK and the ROM separately, and registers **TimeCrisis2VR** in the Steam library. Launch it from there: SteamVR only gives controller input to apps started by Steam.

`tools/build_frame.py --flavor quest` builds the Quest APK from the same tree. That is how the Quest side of the LAN tests was built.

## Controls

The Frame's own controller profile (`/interaction_profiles/valve/frame_controller_valve`) is bound first, and Touch stays as a fallback.

| Action | Frame controllers |
| --- | --- |
| Shoot / move the gun to that hand | Either trigger |
| Leave cover (grip mode) | Hold either grip |
| Credits | Right A |
| Aim guide: laser / off / tracer | Right B |
| Options / resume | Right Menu |
| Back | View tap |
| Recentre / recentre and calibrate height | Hold View 1 s / 3 s |
| Cover mode, in options | Right X |
| Gun angle / world size, in options | Right stick up-down / left-right |
| Pages: options, gun fit and player, graphics, LAN play | Left stick up / down |
| View distance or player side, in options | Left stick left / right |
| Default hand / reset | Right stick click / left stick click |

## Display

SteamVR picks the refresh rate (72 Hz unless the per-app setting says otherwise) and the app follows it. The Graphics page's resolution presets and texture filtering work as on Quest; Default keeps the Frame's own size. Eyes render above the runtime's recommended size, up to 2160 by 2160, the panel's native size.

## Design decisions

- **Android in Lepton, not a Linux build.** The Quest host is already Android and GLES, and Lepton runs it unchanged, so the Frame build is a packaging target, not a second renderer.
- **ROM-free APK.** It can be shared; players bring their own `timecrs2.zip`, as on PC.
- **Lepton quirks.** Lepton read no activity or version from a manifest written with ElementTree's `ns0:` prefix, so the build keeps the `android:` namespace. Lepton has no clipboard service, so a Frame-only patch lets SDL start without one. A normal exit now returns through `SDL_main` so the Android activity finishes cleanly.
- **The runtime owns the refresh rate.** The Frame only offers the rate set in SteamVR, so the app doesn't request one.

## Shared files touched

Everything Frame-only is inside `#ifdef TCVR_STEAM_FRAME`, the Frame manifest and Java, `tools/build_frame.py`, `tools/bootstrap_frame.py`, `tools/patch_sdl_frame.py` and `tools/steam_frame/`. Shared code changes:

- `quest/quest_host.c`: the Frame controller profile and View button, the unattended render probe, plus LAN play and physical cover (below).
- `quest/quest_ui.c`, `quest_ui.h`: one `frame_controls` flag so the options page names the Frame's buttons, and the LAN page.
- `quest/quest_main.c`, `quest/tc2_window.c`, `tools/tc2_overlay.py`, `tools/tc2_reset_overlay.py`: the orderly shutdown path, and the cabinet link.
- `quest/CMakeLists.txt`, `quest/tc2_sources.cmake`, `tools/prepare_tc2.py`: the `TCVR_STEAM_FRAME` option, the link overlay and cabinet-ID adapter, and the macOS build.
- `quest/java/.../LauncherActivity.java`, `MainActivity.java`: ROM import into the Frame's persistent storage.

With `TCVR_STEAM_FRAME` off, the Frame code is compiled out. LAN play and the physical cover upgrade are deliberately shared, so Quest and PC builds get them too.

## Status

- **Worn on the Frame**, on a build of this work before it was rebased onto v0.3.0: start-up and play, physical cover with the SAFE sign, and a LAN game with the Frame hosting and a Quest 3 joining (about three minutes linked).
- **Not yet worn:** this branch as rebased onto v0.3.0, with the new menu pages; the SteamVR keyboard for Join by IP; clean shutdown on device.
- **Host tests:** `tests/test_frame_profile.py` (with and without `--touch`), `test_frame_storage.py`, `test_frame_bundle.py`, `test_frame_dimensions.py`, `test_frame_manifest.py`, `test_sdl_clipboard_patch.py`, `test_tc2_frame_shutdown.py`.
