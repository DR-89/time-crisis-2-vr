# Physical cover at the arcade's own slide

Current state of the physical cover mode (options: cover mode "PHYSICAL DUCKING"). Played on the Steam Frame before the rebase onto v0.3.0; not yet worn on Quest or PC.

## Behaviour

Releasing the arcade pedal makes Time Crisis II slide its camera to a pre-authored hidden pose for the current cover spot: down, left, right or occasionally back, sometimes with a turn. In physical mode the port keeps that slide and asks the player to move the same way:

- **The mark.** From the calibrated upright point (recentre position at the calibrated head height), the head must travel `qcover_reach` along the slide's direction: the slide length clamped to 0.15-0.35 m. Moving at least that far hides; coming back to half of it leaves cover. Moving in other directions does not count.
- **The sign.** While exposed at a known spot, a SAFE signpost (red plate, blinking yellow SAFE, yellow arrow along the slide) stands 0.55 m beyond the mark. The arrow turns green as the head approaches. It is drawn on top of the scene and hidden in cover, while paused, and without head tracking.
- **Confirmation.** Both controllers buzz briefly on entering (0.6) and leaving (0.35) cover.
- **No double move.** While the arcade slides, the lean along the slide direction is taken back in proportion to slide progress (`cover_shift`), so the view lands on the arcade's hidden camera instead of lean plus slide. Sideways and other movement stays free.
- **Turning spots** keep the arcade's turn. The first spot on each route turns about 80 degrees; of the 182 18-entry motions in Stage 1's motion list (assumed, not verified, to all be cover slides) about 40 turn 10 degrees or more.
- **Fallback.** With no cover table installed (travel, menus, attract), the original 20 cm duck rule applies.

## Source of the slide

`quest/tc2_cover_spot.h` reads the local cabinet's camera struct at RAM 0x2F4060: state bit 0 marks a cover table at +0xE0 (int16 -1, int16 n, six pointers to x/y/z int32 and pitch/yaw/roll int16 tables), and +0x68 is the table index (n-1 exposed, 0 hidden). Entry 0 is the hidden pose and entry n-1 the exposed pose, so the slide is known before the player hides. The offset is expressed in the exposed camera's VR frame (x right, y up, -z forward) as raw/16384, and the host scales it like the scene (`qvr_world_scale / qvr_units_per_meter`), so at the default world size a slide appears 1.5 times the raw/16384 figures below. `quest/tc2_window.c` publishes each read one frame late, because RAM read at vblank F describes the camera rendered at F+1.

Every measured Stage 1 slide is 18 entries and takes 17 frames (about 0.3 s). The table layout, addresses and timing were established on the headless core; see `build/hide-probe` while it exists, and the measured spots below.

| Route, spot | Slide in the exposed camera frame (raw/16384) | Turn |
| --- | --- | --- |
| Red 1 | right -1.183, up -0.290, forward +0.342 m | -80.2 deg |
| Red 2 (pitched -27 deg) | up -0.390, forward +0.199 m | 0 |
| Red 3 (pitched -22 deg) | up -0.457, forward +0.184 m | 0 |
| Red 4 | right -0.718, up -0.281, forward +0.029 m | 0 |
| Blue 1 | right +1.084, up -0.289, forward +0.393 m | +79.4 deg |
| Blue 2 | right -0.497, up -0.375 m | 0 |
| Blue 3 | right -0.252, up -0.547 m | 0 |

The production decoder reproduced this table from recorded camera structs of those runs (bot and linked red/blue) against the real data ROM.

## Tests

`python3 tests/test_cover_spot.py` (host, ASan/UBSan): decoding with pitch, yaw, turn and progress; ROM address mapping; rejection of non-cover, malformed, out-of-range and degenerate tables; the directional pedal with its hysteresis, wrong-direction and lost-tracking cases; and the painted SAFE atlas. `SAFE_ATLAS=out.ppm` writes the atlas for inspection. The existing input fixture (`tests/test_frame_profile.py`, with and without `--touch`) still covers the 20 cm fallback.

Open: Quest and PC wear tests, the rebased build on the Frame, spots beyond Stage 1, and the other ROM sets' addresses. Turning spots keep the arcade's turn; a fade through the turn is the fallback if it proves uncomfortable.
