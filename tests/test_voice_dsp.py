"""Voice chat's microphone clean-up (quest/tc2_voice_dsp.c: SpeexDSP echo cancellation, RNNoise noise suppression) on a
simulated clock (tests/voice_dsp_fixture.c): the partner's voice and game noise played through a small room into the
microphone, 40-200 ms late, with the game's queue, the device's buffers, the game stalling and the recorder's jitter; the player
talks over part of it. Measured: about 20 dB of echo off once settled (16 required), the player's voice kept, game noise taken for
speech under 1% of the time.
Built with the local LLVM/MinGW toolchain against the pinned RNNoise and SpeexDSP (tools/bootstrap.py)."""
from pathlib import Path
import re, subprocess, sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from speech import speech  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/voice-dsp-test'
OUT.mkdir(parents=True, exist_ok=True)
RN = ROOT / '.tools/rnnoise/rnnoise-0.2'
SP = ROOT / '.tools/speexdsp/speexdsp-SpeexDSP-1.2.1'
if not RN.exists() or not SP.exists():
    sys.exit('Fetch the voice libraries first: python tools/bootstrap.py (or build once)')
compiler = next((ROOT / '.tools/llvm-mingw').glob('*/bin/x86_64-w64-mingw32-clang.exe'))
types = OUT / 'inc/speex/speexdsp_config_types.h'
types.parent.mkdir(parents=True, exist_ok=True)
types.write_text('#include <stdint.h>\ntypedef int16_t spx_int16_t;\ntypedef uint16_t spx_uint16_t;\ntypedef int32_t spx_int32_t;\ntypedef uint32_t spx_uint32_t;\n')
rn = [RN / 'src' / f'{n}.c' for n in ('denoise', 'rnn', 'pitch', 'kiss_fft', 'celt_lpc', 'nnet', 'nnet_default', 'parse_lpcnet_weights',
                                      'rnnoise_data', 'rnnoise_tables')]
sp = [SP / 'libspeexdsp' / f'{n}.c' for n in ('mdf', 'fftwrap', 'smallft')]
exe = OUT / 'voice_dsp.exe'
subprocess.run([str(compiler), '-std=gnu11', '-O2', '-w', '-DFLOATING_POINT', '-DUSE_SMALLFT', '-DEXPORT=',
                '-I' + str(RN / 'include'), '-I' + str(RN / 'src'), '-I' + str(SP / 'include'), '-I' + str(OUT / 'inc/speex'),
                str(ROOT / 'tests/voice_dsp_fixture.c'), str(ROOT / 'quest/tc2_voice_dsp.c'), *map(str, rn), *map(str, sp),
                '-o', str(exe)], check=True)
far, near = speech('far'), speech('near')
failures = []
# latency: from leaving the game's queue to the microphone (device buffers, the air, the recorder); gain: how loud the speaker is
# heard. A Quest's speakers sit by the microphone; a PC's may be across a desk.
for latency, gain in ((40, 0.6), (120, 0.6), (200, 1.0)):
    run = subprocess.run([str(exe), str(far), str(near), str(latency), str(gain), str(OUT / f'out-{latency}.wav')],
                         capture_output=True, text=True, timeout=300)
    line = run.stdout.strip()
    print(f'{latency:3d} ms, gain {gain}: {line}')
    m = re.match(r'echo ([\d.-]+) dB \| near kept ([\d.-]+) double-talk ([\d.-]+) level ([\d.]+) lag (\d+) \| '
                 r'speech: echo ([\d.]+)% noise ([\d.]+)% near ([\d.]+)%', line)
    if run.returncode or not m:
        failures.append(f'{latency} ms: the fixture failed ({run.stderr[-300:]})')
        continue
    echo, kept, double, level, _, sp_echo, sp_noise, sp_near = map(float, m.groups())
    if echo < 16:
        failures.append(f'{latency} ms: echo only {echo:.1f} dB down once settled (want 16+)')
    if kept < 0.7 or double < 0.6 or not 0.6 < level < 1.4:
        failures.append(f'{latency} ms: the player\'s voice was not kept (alone {kept:.2f}, over the partner {double:.2f}, level {level:.2f})')
    if sp_noise > 3:
        failures.append(f'{latency} ms: game noise taken for speech {sp_noise:.1f}% of the time')
    if sp_near < 40:
        failures.append(f'{latency} ms: the player\'s voice taken for speech only {sp_near:.1f}% of the time')
# the control: noise suppression alone leaves the echo (else the numbers above would not show the echo canceller at work)
run = subprocess.run([str(exe), str(far), str(near), '120', '0.6', str(OUT / 'out-control.wav'), '2'], capture_output=True, text=True, timeout=300)
print(f'control, no echo cancellation: {run.stdout.strip()}')
m = re.match(r'echo ([\d.-]+) dB', run.stdout)
if not m or float(m.group(1)) > 6:
    failures.append(f'without echo cancellation the echo still fell {m.group(1) if m else "?"} dB: the test does not measure it')
for f in failures:
    print('FAIL: ' + f)
if failures:
    sys.exit(1)
print('PASS: echo of the game and partner taken out, the player\'s voice kept, noise not taken for speech.')
