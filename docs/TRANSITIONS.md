# Transition rendering

The arcade uses several 3D model commands to draw screen graphics. Those depths
are layout constants; using them as physical distances produces close floating
fragments when the VR eye moves away from the arcade camera.

The US TSS3 recording and isolated model renders confirm these additional assets:

- **3477:** animated cartridge leaving the ammunition HUD. The original HUD
  animation calls it at `800CDA90/800CDA94`.
- **3478:** black screen mask, including the two cinematic edge rectangles
  (`800CDCC0/800CDCE4`). In the captured transition, the masks span arcade
  x = -0.631..640.631 and y = -175.971..64.502 / 415.498..655.971,
  at a synthetic depth of 0.03125 m.
- **5077..5082:** player/COM screen markers, rendered near 0.047 m. For example,
  isolated model 5079 displays the green COM arrow.

The renderer places these screen assets on the existing 2.5 m HUD plane and clips
their pixels to the arcade viewport. They do not participate in world aiming or
camera selection. Genuine world polygons retain their original geometry.

For model 3478, only wide edge rectangles that stay outside the screen centre are
classified as letterboxing. VR omits those polygons and the matching black text-
layer pixels in every text/priority pass. White WAIT text and other colored text
remain visible. Central/full-screen uses of the mask are preserved. Desktop
rendering retains the original letterboxing.

## Reproduction

The original session recorder stores input changes. The VR host previously
overwrote the current replay state with live input on frames without a new line.
The host now leaves cabinet inputs to the recorder during playback. In the
captured session, the corrected Windows replay matches the Quest's machine-gun
transitions at frames 16,863 and 19,342 and exits at 28,201 with zero CPU traps,
zero floating-point exceptions and zero unknown C435 commands.

For local analysis, `TC2_FRAME_CAPTURE` names an existing output directory and
`TC2_FRAME_RANGE=600:28200:300` selects first:last:step. The Windows renderer writes
its built frames and texture data there. These files contain game data and must
remain in ignored local folders. Snapshots are diagnostics for the matching
checkout, not a portable save-state format.

`tests/render_tc2_frames.py --frames build/transition-frames-live --output artifacts/transition-after`
renders the actual GLES renderer on ANGLE with a translated eye. `--x`, `--y` and
`--yaw` select the viewpoint; `--flat` selects the desktop presentation and
`--baseline <local Git revision>` permits before/after comparisons.

## Validation

- The scene fixture checks HUD projection, aim exclusion, both letterbox edges,
  full-screen masks and similarly shaped world polygons.
- The input fixture checks that 120 ticks without a recorded change preserve
  recorded buttons, aim coordinates and off-screen state.
- 93 captured frames compile and render with the production GLES shaders without
  GL errors. The transition at frame 6,300 retains WAIT and player markers while
  the black bars disappear.
- All 93 desktop frames are pixel-identical to the previous renderer.
- Five transition frames render from both eye positions (64 mm separation)
  without GL errors; dialogue and other text remain visible.
- Both platform builds and APK verification pass. The packaged Windows build
  also completes a 1,501-frame desktop startup smoke test.
- The same game code is released as v0.2.0. Quest footage was captured with test
  build 4. A subsequent PCVR playtest through Virtual Desktop on Quest 3 confirmed
  VR and controls; the runtime reported 90 Hz at 1980 x 2160 per eye on an RTX 3050
  Laptop GPU. This short playtest does not establish full-game compatibility or
  uninterrupted frame delivery in every scene.
