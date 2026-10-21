# Time Crisis II VR for Steam Frame

This is a standalone ARM64 Android/OpenXR build for the Steam Frame, run by
Lepton. The APK contains no arcade ROM files. Supply your own matching Time
Crisis II US TSS3 Ver.B archive (`timecrs2.zip`).

Run `./install.sh --verify-only` to check the package without contacting a
device. To validate your archive and preview the installation plan, run:

```sh
./install.sh --dry-run --rom /path/to/timecrs2.zip
```

To install, enable SSH on the Frame (Developer settings) and close any running
game. Then run:

```sh
./install.sh --install --rom /path/to/timecrs2.zip --host steamos@YOUR_FRAME_ADDRESS
```

Add `--identity /path/to/key` if the Frame only accepts a particular SSH key,
such as the SteamOS Devkit Client's `devkit_rsa`. `--lock-script` optionally
calls a `claim`/`release` booking script around the install, for setups where
several tools share one Frame.

The installer verifies all 22 ROM chips by SHA-256, copies the archive
separately from the APK, backs up any existing `TimeCrisis2VR` title and data
(and verifies the backup), and registers the Steam title with the Lepton
compatibility tool. It does not download or bundle game files.

On SteamOS, the persistent game data is `~/Documents/TimeCrisis2VR`. Inside
Lepton, the app uses `/sdcard/Documents/TimeCrisis2VR`, including `timecrs2.zip`,
the verified `extracted/` chips, weapon models, and `quest-options.cfg`.
