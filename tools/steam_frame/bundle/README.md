# Time Crisis II VR for Steam Frame

This is a local standalone ARM64 Android/OpenXR build for Steam Frame through
Lepton. The APK contains no arcade ROM files. Supply your own matching Time
Crisis II US TSS3 Ver.B archive (`timecrs2.zip`).

Run `./install.sh --verify-only` to check the package without contacting a
device. To validate your archive and preview the installation plan, run:

```sh
./install.sh --dry-run --rom /path/to/timecrs2.zip
```

To install, enable SSH on the Frame, close any running game, and set
`FRAME_LOCK_SCRIPT` to the installed Frame booking script. Then run:

```sh
./install.sh --install --rom /path/to/timecrs2.zip \
  --host steamos@YOUR_FRAME_ADDRESS --lock-script /path/to/frame-lock.sh
```

The installer verifies all 22 ROM chips by SHA-256, saves the archive
separately from the APK, backs up any existing `TimeCrisis2VR` title and data,
and registers the Lepton shortcut. It does not download or bundle game files.

On SteamOS, the persistent game data is `~/Documents/TimeCrisis2VR`. Inside
Lepton, the app uses `/sdcard/Documents/TimeCrisis2VR`, including `timecrs2.zip`,
the verified `extracted/` chips, weapon models, and `quest-options.cfg`.
