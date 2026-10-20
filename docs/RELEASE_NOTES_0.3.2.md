# Time Crisis II Arcade VR 0.3.2 — local Quest build

20 October 2026 · Android version code 10.

- Add **TEXTURE FILTERING: ON / OFF** to the VR pause/options menu. Default is **ON** for new installations and existing configs that have no filtering setting.
- Squeeze the physical **left grip** while paused to toggle; the setting applies immediately and is saved. Grip controls during gameplay are unchanged. Keyboard **F** performs the same action while paused in the shared PC host.
- ON uses bilinear world-texture sampling; OFF restores point sampling. HUD/text and stencil masks retain their existing behavior. Bilinear filtering reduces abrupt texel transitions; it does not add mipmaps or eliminate all distance shimmer.
- Preserve the launcher icons and banners from the preceding local build.

## Validation

Saved defaults, old-config migration, malformed values and ON/OFF persistence pass. The actual host input fixture covers both handedness layouts, held controls at menu entry, focus loss, disconnected controllers and one toggle per press. All 26 menu previews render without GLES errors; live changes refresh the label.

Real GLES tests cover 256 palette/flip/tile-edge comparisons, transparent pens, blending, unchanged overlays/stencil masks and sub-texel motion continuity.

Quest ARM64 APK built and signed locally. This update has not been installed or performance-tested on a headset and has not been published to GitHub.
