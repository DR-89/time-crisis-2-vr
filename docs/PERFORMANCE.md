> Historical Time Crisis (TC1) reference. This document is inherited from the first port and does not describe Time Crisis II validation. See the [TCII README](../README.md) for current status.

# Performance evidence

## Quest 3, v0.6.0, 2026-10-04

A warm 120-second gameplay window with both controllers, weapon rendering and sound active. Startup and diagnostic image capture were excluded. The same app process remained active throughout; original-game frame counter advanced from 10,560 to 17,760. Scenes were user-played, not a deterministic benchmark.

- 120 one-second runtime samples at an observed 120 Hz.
- FPS: mean 120.017, median 120, minimum 115, p05 116, p95 121, maximum 122.
- Runtime CPU+GPU: mean 5.21 ms, p95 6.84 ms, maximum 7.89 ms.
- Stale: 293 out of 14,400 nominal target frames, or 2.035%.
- Temperature counter: 46–47 °C.
- 1374 × 1440 pixels per eye.
- No logged GL errors or emulated CPU/DSP/sound faults.

Mean FPS does not demonstrate perfectly even delivery; stale counts remain nonzero. Values above 120 result from one-second counter boundaries. This is not a complete-game test, and the result is not a new measurement of later versions.

## Implementation

The world uses one CPU traversal with `GL_OVR_multiview2`. Each eye retains its own pose/projection, gamma pass and weapon overlay. Unsupported multiview falls back to separate world draws. Sprite images are shared between eyes and repeated VR poses with a 32 MiB cache and fallback; sprite and text palette work is precomputed. Original vs optimized raster output matched in 256 test cases. Multiview output matched the older renderer for both eyes, gamma cases, texture mutation and perspective polygons in ANGLE fixtures.

Cold textures use the upstream incremental texture budget and may initially be coarser. A single expensive texture can still overshoot the budget. Game and sound code use `-O2`, retaining `-fwrapv` and `-fno-strict-aliasing`; no fast-math is enabled.

`[XRPERF]` reports render wall time after `xrWaitFrame`, excluding game simulation. `[XRSTAGE] thread` is actual thread CPU time. Neither is a GPU timer. Detailed per-polygon profiling is opt-in through `files/profile.request` because the timer calls affect performance.
