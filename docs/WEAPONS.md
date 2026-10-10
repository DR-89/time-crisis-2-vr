# Arcade weapon presentation

The arcade game selects the pistol or the scripted machine gun. There is no
manual weapon inventory. Holding the trigger supplies a continuous input level;
the original game decides when automatic fire is allowed.

## Source of the weapon state

`quest/tc2_weapon.h` reads the original program's player state without modifying
it. This adapter is specific to the pinned US TSS3 Ver. B ROM:

- Startup at `80000384/80000388` sets `gp = 802C7820`.
- The HUD at `800CE5A0` loads the local player from `gp + 7608`
  (`802CEE28`). Only slots 0 and 1 are accepted.
- The following shifts compute `player * 0x474`. At `800CE5C8/800CE5CC`
  it tests bit `0x8` of `802F43D0 + player * 0x474`.
- A set bit starts the machine-gun HUD animation. Its three draws at
  `800CE654`, `800CE68C` and `800CE6D8` use model `0x17C3` (6083).
  When the bit clears, `800CE758..800CE780` restores the cartridge display.
- The reload path at `800137F0..80013800` also tests this bit and restores
  the machine gun's ammunition. The same state selects the VR hand model.

RAM is read on the game thread at the frame boundary, while the translated CPU
is parked. Invalid slots or truncated memory fall back to the pistol. HUD model
6083 shares the 2.5 m HUD plane with the health and cartridge models, rather than
being placed into the world or intercepting aim rays.

## Assets and animation

The existing animated pistol is `models/player-gun.tcgun`. The second slot
loads `models/machine-gun.tcgun`; a missing/invalid file retains the pistol and
writes a clear fallback message to the local log. Models are loaded once at
startup, and changing the selected slot performs no disk access or GL upload.

The gun I/O board's recoil solenoid (`tc2_h8_outputs()`, bit 0) drives the slide
or whole-model recoil, controller haptics and a 42 ms local muzzle flash. These
effects occur on accepted shots, not on trigger pulls while in cover or empty.
Both eyes use the same animation age. Recoil never alters the aiming ray, and
each model supplies its own calibrated muzzle position.

The supplied Tripo3D export is imported with `tools/import_machine_gun.py`.
Its 65 mesh parts and 99,484 triangles are combined into one textured draw with
9,177 triangles, 7,246 vertices and a 2048-square texture atlas with padded edges.
The gun is 45 cm long, aligned along -Z using its barrel axis, with the grip
position and muzzle calibrated from the source geometry. The source SHA-256
and calibration are recorded in `quest/assets/models/machine-gun.json`.

To reproduce the asset, install Python with NumPy and Pillow and Node.js, then run:

```powershell
npm install --prefix .tools/meshoptimizer --ignore-scripts --save-exact meshoptimizer@1.3.0
python -X utf8 tools/import_machine_gun.py "C:\Users\Dirk\Downloads\compact+submachine+gun+3d+model.glb" quest/assets/models/machine-gun.tcgun
```

The importer deliberately accepts only this inspected export. Its calibration
must be revisited for a different source. Use `--triangles 0` with a separate
output path to generate the full-geometry reference for comparison.

## Local validation, 2026-10-10

- Test build 3 compiles for Android ARM64 and Windows x64. APK verification
  checks both production models, ROM checksums and the immersive entry point.
  The Windows ZIP passes its manifest hashes and a clean 1,501-frame desktop
  startup run with both models loaded, zero CPU traps and zero FP exceptions.
- The installed Quest 3 reports version `0.1.0-test.3`, loads both model files
  with the expected SHA-256 hashes and confirms 120 Hz. During the live session,
  the arcade selects the machine gun at frame 16,863 and restores the pistol at
  frame 19,342 (about 41 seconds). No GL errors are logged in that interval;
  runtime cadence samples report 120 Hz. Stereo captures verify the pistol and
  HUD, while the machine-gun interval is confirmed by the state log. Subjective
  grip/animation feedback is pending; PCVR headset testing remains deferred.
- The input fixture preserves the held trigger for 120 simulation ticks.
- Weapon-state fixtures cover both player slots, endian order, unrelated flags,
  invalid slots and bounded reads. These synthetic tests complement the observed
  state transitions on Quest.
- Production GLES rendering on ANGLE passes stereo, slide/trigger motion,
  flash lifetime, return to rest, failed asset loading and per-model muzzle tests.
  It renders both production assets, switches to the machine gun and back through
  the arcade state decoder, and compares both stereo views. The optimized gun's
  side-view silhouette has 0.9881 intersection-over-union with the full reference.
- `artifacts/pistol-shot-preview.gif` and `artifacts/machine-gun-shot-preview.gif`
  show the real shader animations at one third speed for inspection; production
  timing remains 150 ms recoil / 42 ms flash.
