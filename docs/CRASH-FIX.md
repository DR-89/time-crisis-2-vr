> Historical Time Crisis (TC1) reference. This document is inherited from the first port and does not describe Time Crisis II validation. See the [TCII README](../README.md) for current status.

# Stage 1 explosive-box regression

The reported crash was reproduced on Quest 3 after the helicopter/car sequence.
The input recorder captured a failure at **frame 12,087**, with:

```text
[TRAP] f12087 at 0x006564 -> 0x0004A4A2: jump to an address that is no known instruction
[RR] call frame 5 returned without rts -- stopping
```

Android recorded an application exit with status 5. A Windows headless replay of
the same input recording and initial EEPROM failed at the identical frame and
address. This identifies missing translated game code, rather than a headset
memory error or a rendering timeout, as the cause of this report.

`tools/translate_crate.py` translates the **318 original instructions** from
`0x4A4A2` through `0x4AB97` at build time. A SHA-256 guard accepts only the matching
TS2 Ver.B ROM region. It preserves the original effects, flags, calls, stack
writes, scheduling polls and coroutine continuations. It rejoins upstream at
the valid instruction boundary `0x4AB98`; the upstream `0x4AB80` disassembly
starts inside an immediate instruction. The upstream dispatcher uses this
additional translation only when its normal lookup has no instruction.

The corrected private recording has passed:

- Windows headless replay through frame 13,200 with zero traps.
- Windows OpenGL replay through frame 15,000 with zero traps and no DSP/sound faults.
- Shared GLES multiview/reference image tests, with byte-identical eye images.

These checks establish the recorded regression. They do not establish that every
unvisited explosion, boss or stage has complete translation coverage.

To export a new report before another launch overwrites the previous recording:

```powershell
./Export-Quest-Diagnostics.ps1
python tests/test_pc.py --replay 'C:\path\to\last-session.inputs' --frames 15000
```

The exporter includes current/previous input recordings, logs and Android exit
information. Reports remain local under `artifacts`; they are not release assets.
