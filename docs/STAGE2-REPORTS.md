> Historical Time Crisis (TC1) reference. This document is inherited from the first port and does not describe Time Crisis II validation. See the [TCII README](../README.md) for current status.

# Stage 2 crash reports

## Status: awaiting a recorded failing session

[Issue #3](https://github.com/DR-89/time-crisis-vr/issues/3) reports two exits to
Quest Home: shooting at the chandelier, and leaving cover near the stairs in
Stage 2. Neither report currently includes the game log, Android exit reason or
input recording. They might share a cause, but that has not been established.

The earlier Stage 1 explosive-box crash was traced to missing translated arcade
instructions and fixed from the original ROM. That diagnosis is not proof of the
cause of these Stage 2 reports. No Stage 2 fix is claimed by v0.8.4.
The built-in autoplay does not provide a reproduction of these scenes.

The development Quest's existing session was exported before installing the
angle test and replayed with reconstructed scene rendering through frame 34,600.
It completed with zero CPU traps and no reported graphics/DSP/sound faults. The
capture at frame 34,000 shows **STAGE 2 CLEAR**. This establishes that this
particular recorded route completes Stage 2; it does not reproduce either
reporter's specific chandelier shot or stairs failure.

The subsequent Quest 3 playtest of `0.8.4-angle-test.1` ended normally at frame
31,016 (Android `EXIT_SELF`, status 0), with no reported CPU traps or graphics/
DSP/sound faults. The tester confirmed the gun-angle setting and Stage 2 test
worked without a crash. Both current/previous logs and input recordings were
saved. The two original reports remain unresolved; this result does not establish
their cause or imply a mistake by either reporter.

## Collect a report

The existing v0.8.3 release already records the arcade inputs needed for replay.
The diagnostic helper is available as **TimeCrisisVR-v0.8.4-diagnostics.zip** on
the [v0.8.4 release page](https://github.com/DR-89/time-crisis-vr/releases/tag/v0.8.4).
After a crash, leave the app closed and keep it installed. Connect the Quest to a
Windows PC with Android platform-tools, then allow USB debugging in the headset.
Run this script from the source checkout or the supplied diagnostic helper:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Export-Quest-Diagnostics.ps1
```

If ADB is installed elsewhere, add `-Adb 'C:\path\to\adb.exe'`. If more than one
Quest is connected, add `-Serial YOUR_QUEST_SERIAL`. The script selects only a
Quest automatically and does not launch, reinstall or clear the game.

The resulting `artifacts\diagnostics-*.zip` contains current/previous game logs,
current/previous input recordings, saved options, installed package information
and Android exit details. Missing optional files are listed in `report.txt`.
Nothing is uploaded automatically. Input recordings contain arcade controls and
the starting EEPROM; they do not contain headset video, microphone audio or head
poses. Inspect the ZIP before sharing it with the maintainer.

Include the app version, headset model, which scene failed, and whether the exit
happened when shooting, leaving cover or moving to the next position. The app
keeps one previous session: repeated launches can overwrite the relevant report.

## Reproduce and verify a repair

Replay the provided input file in the Windows build, including its starting
EEPROM, past the failing frame. If it reproduces only with rendering, run a
desktop replay with `TCVR_SCENE_VIEW=1`; a headless pass does not validate GPU
behaviour. Match any missing-code target against the supplied ROM before
changing translation. Verify the exact recorded failure on the corrected build
and then ask for a Quest retest at the same scene.
