# v0.3.0 feedback features and validation, 2026-10-12

Version `0.3.0` (Android code 8) publishes the changes tested locally as
`0.2.0-feedback-test.1` (code 7). The features also compile into the shared
Windows OpenXR host. A local Quest recording confirms the new Namco model,
Tracer option and scale controls in live gameplay; it remains a short test.

## Tracer Squeeze

The aim button cycles LASER → OFF → TRACER → LASER: B in the default right-hand
layout, Y in the left-hand layout. Existing saved laser preferences are retained.
TRACER shows an orange line only for 65 ms after the original I/O board accepts a
shot. Empty/covered trigger pulls do not produce a fake shot. The line fades out,
and its endpoints stay at that shot's muzzle and existing raycast hit rather
than following subsequent hand movement. No additional raycast is performed.
Both eyes use the same timestamp, independently of 72/90/120 Hz refresh.
Focus loss, pause and hand changes clear the effect. The flash is for tracked
VR; desktop mode retains its mouse aiming guide.

## Namco gun and grip fit

The user supplied `namco-arcade-gun.zip`, containing Martoscar's
[Namco Arcade Gun](https://sketchfab.com/3d-models/namco-arcade-gun-15fbd5b9add94a34b5c21746e3dd32be),
under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution and
modifications are recorded in NOTICE.md, which is included in the APK.

`tools/import_namco_gun.py` retains all 13,149 vertices / 17,108 triangles,
combines the model into one draw and bakes its base colors into a padded 1024²
atlas. Missing normals are reconstructed. Normal/roughness maps are omitted;
the existing lightweight gun shader supplies lighting. The source is about
24.75 cm long. The axis conversion, upper-grip anchor, muzzle and source hash
are recorded in `quest/assets/models/namco-gun.json`. Recoil moves the whole
model; the supplied geometry has no independent slide/trigger parts. The older
CC0 pistol remains a fallback if the new asset cannot load.

Reproduce with Python, NumPy and Pillow:

```powershell
python tools/import_namco_gun.py "$env:USERPROFILE\Downloads\namco-arcade-gun.zip" quest/assets/models/namco-gun.tcgun
```

Open options, then move the left stick up/down to **GUN FIT / PLAYER**.
Right stick up/down changes **GUN UP**; right stick right/left changes **GUN BACK**.
Both use 5 mm steps over −200..+200 mm. Left stick click resets both to zero.
Offsets rotate with the controller, before the pitch correction, and move model,
muzzle and aim origin together. X is unchanged. They remain physical millimetres
when WORLD SIZE changes. In the saved `quest-options.cfg`, the keys are
`gun_offset_y_mm` and `gun_offset_z_mm`; positive Y means up and positive Z
means back towards the user. The new grip anchor is a geometry-based starting
point, not a measured controller-to-shell calibration. A headset check is still
needed. In Windows options, Tab changes page, arrows adjust, Home resets.

## Robert's original route

On GUN FIT / PLAYER, left stick left/right selects P1 Keith / P2 Robert.
The preference (`player_side=1` or `2`) is latched before boot; completely close
and reopen the app to apply it. Windows Page Down/Up selects the same preference.

The pinned System 23 core is standalone and normally initializes local input
slot 0. Selecting a cabinet switch alone does not select Robert's route.
The adapter therefore sets the valid original route at `802F3FD4` immediately
before the load at `80012E8C`, which passes it to original initializer `80013058`.
That function chooses each character's original data and the later game code
uses the same route for its camera and scene behavior. The adapter accepts only
native modes 1/2 and route 0/1. P1 never writes game RAM. No per-frame route
overwrite, synthetic camera mirroring or network cabinet is used. The original
local input/HUD slot remains 0, so this is a standalone Robert route, not full
linked two-player emulation.

The TSS input's active-low bit 2 also reflects the selected cabinet switch,
preserving trigger and pedal bits. Hardware mapping references:
[MAME TSS I/O](https://github.com/mamedev/mame/blob/master/src/devices/bus/jvs/namcoio.cpp)
and [MAME System 23 inputs](https://github.com/mamedev/mame/blob/master/src/mame/namco/namcos23.cpp).
Build adapters are generated from exact pinned source markers and fail if the
markers change. The upstream submodule and ROM files are left intact.

Both 2,800-frame replays reach live stage-one gameplay with zero CPU traps or
floating-point exceptions. Real renderer captures at frames 1500, 2100 and 2700
show different corridor viewpoints, character placements and combat progress.
Robert remains on the opposite restaurant view while the Keith recording has
already reached the window scene. The recording was made for Keith, so its aim
does not match Robert's targets. This is not a complete Robert-route playthrough.
Both extended 28,201-frame replays also finish with zero CPU traps, floating-point
exceptions or unknown C435 commands. The Keith replay switches to the machine
gun at frame 16863 and back at 19342. Robert's replay re-enters the native player
initializer and retains the selected route; its different target positions mean
the Keith input recording is not evidence of completing Robert's stages.

## Scale and validation limits

The earlier world-size fix remains: 25–400%, applied to scene geometry and
surface aiming immediately, including paused edits. TC2's 100% calibration
measures complete opening figures at about 1.70–1.90 m. Physical OpenXR eye poses
are preserved; no guessed physical IPD is imposed. TC1 has a separately installed
calibrated test build. Perceived scale still needs the user's headset assessment.

Automated checks cover saved settings, both control layouts, offset/muzzle
alignment, route bounds, 21 menu previews, real GLES tracer visibility/lifetime,
stereo weapon rendering and world-scale surface aiming. Windows and Android
ARM64 builds pass. These checks do not establish sustained 120 FPS or comfort.
