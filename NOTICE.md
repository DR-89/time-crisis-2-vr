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
- **Valve OpenVR keyboard ABI header:** BSD-3-Clause, Copyright (c) 2015 Valve Corporation;
  v2.5.1 commit `ae46a8dd0172580648c8922658a100439115d3eb`. Unmodified header and
  license are in `quest/vendor/openvr`; the license accompanies APK/Windows packages.
  No OpenVR runtime binary is bundled.
- **Windows OpenGL loader:** generated with [glad 2.0.8](https://github.com/Dav1dde/glad);
  generated code and Khronos declarations retain their notices in `pc/vendor/glad`.
- **Windows dependencies:** zlib 1.3.1, SDL 2.30.11, Khronos OpenXR 1.1.43,
  LLVM/MinGW runtime and winpthreads. Their notices are included under `licenses`
  in the portable Windows package.
- **Namco Arcade Gun model:** [Namco Arcade Gun by Martoscar](https://sketchfab.com/3d-models/namco-arcade-gun-15fbd5b9add94a34b5c21746e3dd32be),
  licensed under [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).
  Supplied by the user as `namco-arcade-gun.zip`. Converted for this port:
  axes and grip/muzzle calibration, reconstructed missing normals, baked base
  colors into a 1024-square atlas, omitted normal/roughness maps, combined mesh
  parts and added whole-model recoil. Original geometry is retained (17,108
  triangles). Source checksum and conversion details are recorded in
  `quest/assets/models/namco-gun.json`. This independently created model is not
  extracted from Time Crisis and is not covered by the port's MIT license.
- **Fallback player weapon model:** [Lowpoly Pistol by FireWarden](https://opengameart.org/content/lowpoly-pistol),
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

## Cabinet-link research

The port-owned `link/tc2_link.c` implements the original C139/C422 register and
DMA boundary. Functional research references are MAME's Namco C139 device
(Angelo Salese, BSD-3-Clause), [SailorSat's C139 work](https://github.com/mamedev/mame/pull/13421),
and [MAME-TC2-Public at 5ff99b9](https://github.com/pocketjazzy/MAME-TC2-Public/commit/5ff99b9da5b528de1c5be484115ea3dc014efd3a).
The adapter is an independent implementation; it does not incorporate that fork's
ROM code patches, game RAM repairs, heartbeat fabrication or packet replay.
