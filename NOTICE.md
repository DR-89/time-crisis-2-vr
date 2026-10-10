# Attribution and license scope

Time Crisis II VR is an unofficial port for Meta Quest 3 and Windows. It is not
affiliated with or endorsed by Namco, Bandai Namco, or Meta.

- **VR port code:** DR-89 and contributors, MIT; see [LICENSE](LICENSE).
- **Underlying game reconstruction and shared engine:**
  [spacestate1/namco22-decompile](https://github.com/spacestate1/namco22-decompile),
  pinned at `0837a397ebe554dbf5341d95629aa1e29fc58a52`, using its `tc2/` System 23 core.
  Its MIT notice credits **Copyright (c) 2026 cmcrann** and is preserved in
  the submodule and APK's `assets/licenses/namco22-LICENSE.txt`.
- **SDL 2.30.11:** zlib license; original notice included in the APK.
- **Khronos OpenXR loader 1.1.43:** its original license notice is included in the APK.
- **Windows OpenGL loader:** generated with [glad 2.0.8](https://github.com/Dav1dde/glad);
  generated code and Khronos declarations retain their notices in `pc/vendor/glad`.
- **Windows dependencies:** zlib 1.3.1, SDL 2.30.11, Khronos OpenXR 1.1.43,
  LLVM/MinGW runtime and winpthreads. Their notices are included under `licenses`
  in the portable Windows package.
- **Player weapon model:** [Lowpoly Pistol by FireWarden](https://opengameart.org/content/lowpoly-pistol),
  released under CC0. Imported from the user-supplied QuestRetroDepth header, with
  separate body, trigger and slide parts. Provenance, geometry and checksums are
  recorded in `quest/assets/models/player-gun.json`. The model is not extracted
  from Time Crisis. The earlier Tripo3D model remains in the Git history.
- **Machine-gun model:** generated with Tripo3D and supplied by the user as
  `compact+submachine+gun+3d+model.glb`. Imported and optimized for this port;
  source checksum, calibration and conversion details are in
  `quest/assets/models/machine-gun.json`. This model is not an original arcade
  asset and is not covered by the port code's MIT license.
- **OpenXR host and controller features:** adapted from
  [DR-89/time-crisis-vr v0.8.4](https://github.com/DR-89/time-crisis-vr/tree/v0.8.4), MIT.
- **Time Crisis II, original game program, graphics, sound, ROM contents and trademarks:**
  belong to their respective original rights holders. They are not covered by this
  project's MIT license. Bundled APK and Windows packages include the supplied game files;
  a ROM-free APK requires the user to supply them separately.

The repository contains the port, build/import tools and a reference to upstream.
ROM archives, extracted ROMs, signing keys, generated build output and APKs are
excluded from Git. Downloadable binaries are distributed separately as GitHub
release assets at https://github.com/DR-89/time-crisis-2-vr/releases.
