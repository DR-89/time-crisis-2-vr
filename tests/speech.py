"""Synthetic voices for the voice-chat tests: Windows' own speech synthesizer (System.Speech, David and Zira) saying a few game
call-outs, 48 kHz mono 16-bit. Noise suppression passes speech and stops tones, so the tests talk instead of beeping. Made once
into build/speech and reused."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / 'build/speech'
LINES = {
    'near': ('Microsoft David Desktop', 'Cover me, I am reloading. Two soldiers on the left balcony. Watch out for the knife guy. '
                                        'Nice shot partner, keep moving. The boss is coming from the train. Shoot the red ones first.'),
    'far': ('Microsoft Zira Desktop', 'I have got the right side covered. Reload now, I will hold them off. There is a helicopter above us. '
                                      'Great work, we are almost through this stage. Do you want to continue after this? Behind the crates, three of them.'),
}
SCRIPT = r'''param([string]$Out, [string]$Voice, [string]$Text)
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$s.SelectVoice($Voice)
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(48000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
$s.SetOutputToWaveFile($Out, $fmt)
$s.Speak($Text)
$s.Dispose()
'''


def speech(which: str) -> Path:
    """The path of a 48 kHz mono WAV of one voice ('near': David, 'far': Zira), made on first use."""
    out = CACHE / f'{which}.wav'
    if out.exists() and out.stat().st_size > 48000:
        return out
    CACHE.mkdir(parents=True, exist_ok=True)
    script = CACHE / 'speak.ps1'
    script.write_text(SCRIPT)
    voice, text = LINES[which]
    subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script), '-Out', str(out),
                    '-Voice', voice, '-Text', text], check=True, timeout=120)
    return out
