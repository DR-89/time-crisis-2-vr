> Historical Time Crisis (TC1) reference. This document is inherited from the first port and does not describe Time Crisis II validation. See the [TCII README](../README.md) for current status.

# Camera alignment and Quest 2 refresh handling

Implemented in v0.8.3. The tester confirmed the corresponding test build worked
on Quest 3 before publication. Quest 2 refresh-rate measurements remain pending.

## Flat layers and aim

The old flat shader put arcade pixels on a plane at 2.5 m with 0.005 m per pixel,
equivalent to a 500 px focal length. The reconstructed world uses each polygon's
actual camera. A 15,000-frame replay of the recorded Stage 1 crash session found
one camera in all 14,323 frames with camera data: focal length 772.5625 px and
centre (320, 240). The old flat layer was therefore 1.545125 times too wide.

The flat layer now uses the dominant camera in each prepared arcade frame,
retains it through frames without polygons, and starts with that measured Time
Crisis camera before the first 3D frame. Both stereo shaders use the same camera
uniform, with a draw barrier only when it changes. The fallback aim plane uses
the same projection. The regular PC monitor view keeps its original 500 px
camera and mouse mapping.

Tiles 9368–9373 with sort key zero form the 33×33 shot-mark animation. Their
centres are raycast against the nearest reconstructed polygon once per game
frame. The four corners are then placed at 98% of that depth, preserving the
original arcade pixels while providing surface-depth stereo parallax. No-hit
marks stay on the correctly aligned flat plane. Other HUD art stays flat.
The per-frame screen grid has a fixed memory budget and falls back to a full
triangle scan on overflow. Cached positions are reused for both eyes and repeated
headset frames.

The replay observed 1,781 animated mark instances at 136 distinct positions;
all six tiles were 33×33, and all found surfaces. Depths ranged from 7.73 m to
2,942.52 m in the port's existing world scale. This longer recording includes
far-background hits; it does not imply that every hit is nearby or that the
world scale is physically calibrated.

## Display refresh

The manifest now explicitly includes `quest2`, alongside Quest 3 and Quest 3S.
According to [Meta's manifest documentation](https://developers.meta.com/vr/documentation/native/android/mobile-native-manifest/),
omitting a device can invoke compatibility mode. This is a plausible explanation
for a restricted Quest 2 refresh-rate list, not a confirmed device measurement.

The host selects 120 Hz if enumerated, otherwise the highest supported rate
below the requested value (for example 90 Hz). The existing `refresh-rate.txt`
override can request 90 Hz for a comparison. Requests are checked after focus,
retried at bounded two-second intervals and confirmed using the runtime's actual
rate. Logs separately report runtime frame cadence, which can differ with
compositor half-rate operation. Unsupported rates are never forced; OpenXR
[explicitly defines refresh changes as requests, not guarantees](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrRequestDisplayRefreshRateFB.html).
The arcade simulation remains at approximately 59.906 Hz.

## Validation

- Math tests cover off-centre pixels, camera changes and retention, translated
  controller origins, nearest surfaces, no-hit fallback, all six mark tiles,
  unchanged desktop projection and grid overflow.
- A GPU pixel test compares a grid of flat sprites with reconstructed polygons:
  the corrected projection matches exactly; the old 500 px projection differs.
- Both multiview eyes match the single-view reference, including camera changes,
  gamma correction, texture writes and batching.
- Sprite-cache tests preserve all 16 images and text priority masks.
- Shared-host tests check actual-rate confirmation, delayed changes, bounded
  retries, focus handling and the existing controller interactions.
- The full 15,000-frame rendered replay completes without CPU traps or reported
  graphics/DSP faults. Accelerated replay is not a real-time performance test.
- The tester confirmed the installed aim-test build worked on Quest 3. The
  release retains the same native code and assets, with updated version metadata.
- Quest 2 hardware refresh measurements, a new live PCVR comparison and a full
  playthrough remain pending. No new claim of stable 120 FPS is made.
