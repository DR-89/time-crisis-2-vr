# Time Crisis II VR - Windows

Start `Play VR.cmd` with a connected headset and an active OpenXR runtime supporting OpenGL. Quest Link, Virtual Desktop or SteamVR must already be configured. `Play Desktop.cmd` runs without a headset.

This is the arcade System 23 port. The original ROM chips are in `extracted/`; do not rename them. For desktop controls, use the mouse to aim, left click to shoot, right click or Space for the pedal, 5 for coins and Escape for options. Headset controls match the main project README.

This development build is not yet verified through a complete game. Logs are written to `tc2.log`; input recordings use `tc2_last.rec` and `tc2_prev.rec`.

The desktop and spatial-renderer input replays pass, including an 18,270-frame run of a captured Quest session. Headset testing over Virtual Desktop / Quest Link is deferred; this package is not yet confirmed in PCVR. The native Quest build is tested separately.
