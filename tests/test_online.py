"""Online twin-cabinet play: two real desktop instances, linked through the built-in host or a local lobby server.

Needs a Windows build (tools/build_pc.py). Each scenario starts cabinet A (hosts) and cabinet B (joins), both booted as P1 so the
session's cabinet assignment is exercised, feeds both the same scripted coins / start / fire (an arcade input recording), and
reads the game's own link state from the [ONLINE] status lines: the C422 driver's alive flag, its error counters and the lag.
Room scenarios run upstream's current nmn-server (build/nmn-server-head, see docs/ONLINE.md) on 127.0.0.1 only. Microphones are replaced by
WAV files (a tone for A, silence for B), so no real microphone is opened; what B plays back is recorded and analysed.

    python -X utf8 tests/test_online.py                 # every scenario (18 scenarios + crossplay, ~35 minutes, real time)
    python -X utf8 tests/test_online.py basic room      # some
    python -X utf8 tests/test_online.py --keep          # keep screenshots: build/online-test/<scenario>/{A,B}/cap-*.ppm
"""
from pathlib import Path
import math, os, re, shutil, struct, subprocess, sys, time, wave

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'artifacts/pc/TimeCrisis2VR-PC'
# $TC2_TEST_BUILD: another checkout's build folder (a git worktree reuses the cabinet folders Windows' firewall already allows)
BUILD = Path(os.environ.get('TC2_TEST_BUILD', ROOT / 'build'))
WORK = BUILD / 'online-test'
SERVER_EXE = BUILD / 'nmn-server-head/target/release/nmn-server.exe'   # upstream's current server: it knows Time Crisis 2's 1 KB frames
SERVER_PORT = 27751
STATUS = re.compile(r'\[ONLINE\] frame (\d+) side P(\d) link (on|off|stand-in) alive (\d) lag (\d+) state (\d+) tx (\d+) rx (\d+) '
                    r'drop (\d+) bad (\d+)/(\d+) game-errors (\d+) ping (-?\d+) \| voice (on|off) sent (\d+) received (\d+) lost (\d+)')
TONE_HZ = 440


def recording(path: Path, frames: int, aim_until: int = 0, coins_at: int = 1500, again_at: int = 0):
    """Coins and start at 1500 / 1700 (or coins_at / +200), then the pedal held and the trigger pulsed while the aim sweeps
    (held on the mode screen's top panel until aim_until); again_at: coins once more (a cabinet that restarted)."""
    events = [(0, '0000', 457, 161)]
    for at in (coins_at, again_at):
        if not at:
            continue
        for i in range(8):
            events += [(at + 12 * i, '0001', 457, 161), (at + 12 * i + 6, '0000', 457, 161)]
    events += [(coins_at + 200, '0010', 457, 161), (coins_at + 206, '0000', 457, 161)]
    f, k = coins_at + 400, 0
    while f < frames:
        x, y = (457, 161) if f < aim_until else (int(457 + 250 * math.sin(k * .37)), int(161 + 80 * math.sin(k * .23)))
        # release the pedal now and then to reload
        pedal = '0020' if (k % 40) < 34 else '0000'
        fire = '0030' if pedal == '0020' else '0000'
        events += [(f, fire, x, y), (f + 4, pedal, x, y)]
        f += 8
        k += 1
    events = [e for e in events if not (again_at and again_at <= e[0] < again_at + 96 and e[1] not in ('0001', '0000'))]
    events.sort(key=lambda e: e[0])                       # the replay reads in order
    lines = ['# TC2 session: F frame inputs gunx guny off'] + [f'F {f} {i} {x} {y} 0' for f, i, x, y in events]
    path.write_text('\n'.join(lines) + '\n')


def microphone(path: Path, seconds: int, tone: bool):
    """48 kHz mono: a 440 Hz 'voice' one second on, one second off (or silence)."""
    rate = 48000
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        frames = bytearray()
        for n in range(seconds * rate):
            on = tone and (n // rate) % 2 == 0
            frames += struct.pack('<h', int(9000 * math.sin(2 * math.pi * TONE_HZ * n / rate)) if on else 0)
        w.writeframes(bytes(frames))


def voice_check(path: Path):
    """What B played back: enough sound, and 440 Hz the strongest tone in it."""
    import numpy
    if not path.exists() or path.stat().st_size <= 44:
        return 'no voice was played back'
    with wave.open(str(path)) as w:
        pcm = numpy.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(numpy.float64)
    if len(pcm) < 48000:
        return f'only {len(pcm) / 48000:.2f} s of voice was played back'
    loud = numpy.sqrt(numpy.mean(pcm ** 2))
    spectrum = numpy.abs(numpy.fft.rfft(pcm[:48000 * (len(pcm) // 48000)]))
    peak = numpy.fft.rfftfreq(48000 * (len(pcm) // 48000), 1 / 48000)[spectrum.argmax()]
    if loud < 500:
        return f'played back too quietly (RMS {loud:.0f})'
    if abs(peak - TONE_HZ) > 5:
        return f'played back {peak:.1f} Hz, sent {TONE_HZ} Hz'
    return None


SCENARIOS = {
    # name: (frames, A script, B script, extra env, checks)
    'basic':  (4200, '600:host', '620:join=127.0.0.1', {}, 'steady'),
    'badnet': (4200, '600:host', '620:join=127.0.0.1', {'TC2_NET_SIM': '5,150'}, 'steady'),
    'leave':  (4800, '600:host', '620:join=127.0.0.1;2700:leave;3000:join=127.0.0.1', {}, 'rejoin'),
    'freeze': (4200, '600:host', '620:join=127.0.0.1;2700:freeze=3000', {}, 'recover'),
    'late':   (4200, '600:host', '2400:join=127.0.0.1', {'TC2_NO_MODE_MENU': '1'}, 'late'),   # B links into a game under way
    'boot':   (3000, '30:host', '40:join=127.0.0.1', {}, 'steady'),
    'discover': (3000, '600:host', '620:discover', {}, 'steady'),
    'longfreeze': (5400, '600:host', '620:join=127.0.0.1;2700:freeze=8000', {}, 'recover'),
    'hostfreeze': (5400, '600:host;2700:freeze=8000', '620:join=127.0.0.1', {}, 'recover'),
    # the lobby server: a room with a code, the browser, voice
    'room':   (3600, '600:hostroom', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7'}, 'voice'),
    'browse': (3000, '600:hostroom', '700:browse;820:pickroom', {'TC2_NET_ROOM_CODE': 'KXQ7'}, 'steady'),
    'mute':   (3600, '600:hostroom;2400:mute', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7'}, 'mute'),
    'voicenet': (3600, '600:hostroom', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TC2_NET_SIM': '5,150'}, 'voice'),
    'duplex': (3600, '600:hostroom', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7'}, 'duplex'),
    'roomleave': (4800, '600:hostroom', '700:joincode=KXQ7;2700:leave;3000:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7'}, 'rejoin'),
    # P2 the arcade's way: linked first, A starts LINK PLAY, B puts coins in once A's game is under way and takes the top panel
    'p2join': (6000, '600:hostroom', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502',
               'AIM_UNTIL': 2600, 'B_COINS_AT': 3000, 'B_AIM_UNTIL': 5000}, 'p2join'),
    # the arcade's own way in: A, credited without a room, finds SOLO/LINKED open (the stand-in answers from power-on) and takes
    # it at 1700; that choice hosts a room, no restart. B joins by code a while into A's game, restarts once for its handshake,
    # puts coins in at 4800 and takes LINK PLAY on the right
    'modescreen': (8400, '', '3600:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502,2CE7B7',
                   'AIM_UNTIL': 3400, 'B_COINS_AT': 4800, 'B_AIM_UNTIL': 7400}, 'modescreen'),
    # drop-in: A hosts a room and plays alone at once (the stand-in makes SOLO/LINKED available: A takes it at 1700); B joins by
    # code a minute later, A's game well under way: B restarts once for the handshake, puts coins in at 4800 (no agreed-start
    # wait: A's game has run over 25 s) and takes LINK PLAY on the right
    'dropin': (8400, '600:hostroom', '3600:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502',
               'B_COINS_AT': 4800, 'B_AIM_UNTIL': 7400}, 'dropin'),
}
ROOM_SCENARIOS = {'room', 'browse', 'mute', 'voicenet', 'duplex', 'roomleave', 'modescreen', 'p2join', 'dropin'}


def cabinet(directory: Path, server: str):
    """The two cabinets always run from the same two folders: Windows asks for firewall permission once per executable path,
    and the tests only use loopback, so a fresh path per scenario would only produce prompts. Per-run state is reset."""
    directory.mkdir(parents=True, exist_ok=True)
    for name in ('TimeCrisis2VR.exe', 'openxr_loader.dll'):
        shutil.copy2(PACKAGE / name, directory / name)
    for name in ('extracted', 'models'):
        shutil.copytree(PACKAGE / name, directory / name, dirs_exist_ok=True)
    for stale in [*directory.glob('cap-*.ppm'), *directory.glob('*.log'), *directory.glob('*.rec'), *directory.glob('*.nv'), *directory.glob('*.wav')]:
        stale.unlink()
    (directory / 'quest-options.cfg').write_text('player_side=1\n')
    (directory / 'online.cfg').write_text(f'name=CAB{directory.name}\nserver={server}\n')


def run(name: str, keep: bool):
    frames, script_a, script_b, extra, check = SCENARIOS[name]
    extra = dict(extra)
    base = WORK / name
    run_dir = WORK / 'cabinets'
    rec = base / 'inputs.rec'
    base.mkdir(parents=True, exist_ok=True)
    server = f'127.0.0.1:{SERVER_PORT}' if name in ROOM_SCENARIOS else 'zonesync.invalid'
    for side in 'AB':
        cabinet(run_dir / side, server)
    recording(rec, frames, int(extra.pop('AIM_UNTIL', 0)), again_at=int(extra.pop('A_COINS_AGAIN', 0)))
    recs = {'A': rec, 'B': rec}
    if 'B_COINS_AT' in extra:                                # B's own inputs: it puts coins in later
        recs['B'] = base / 'inputs_b.rec'
        recording(recs['B'], frames, int(extra.pop('B_AIM_UNTIL', 0)), int(extra.pop('B_COINS_AT')))
    seconds = frames // 60 + 30
    microphone(run_dir / 'A' / 'mic.wav', seconds, tone=True)
    microphone(run_dir / 'B' / 'mic.wav', seconds, tone=check == 'duplex')   # both talking at once: the echo safeguard must not mute either
    lobby = None
    if name in ROOM_SCENARIOS:
        if not SERVER_EXE.exists():
            sys.exit("Build upstream's current lobby server first (see docs/ONLINE.md, Testing)")
        lobby = subprocess.Popen([str(SERVER_EXE), '--port', str(SERVER_PORT), '--bind', '127.0.0.1', '--max-per-ip', '16', '--quiet'],
                                 stdout=open(base / 'server.log', 'w'), stderr=subprocess.STDOUT)
        time.sleep(0.5)
    procs = []
    for side, script in (('A', script_a), ('B', script_b)):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', TCVR_TEST_HIDDEN='1', TC2_REPLAY=str(recs[side]), TC2_NET_STATUS='1', TC2_NET_SCRIPT=script,
                   TC2_VOICE_IN=str(run_dir / side / 'mic.wav'), TC2_VOICE_OUT=str(run_dir / side / 'voice_out.wav'), **extra)
        if keep:
            env.setdefault('TCVR_CAPTURE_EVERY', '600')
        procs.append(subprocess.Popen([str(run_dir / side / 'TimeCrisis2VR.exe'), '--desktop', '--no-dialog', '--frames', str(frames)],
                                      cwd=run_dir / side, env=env))
    for p in procs:
        p.wait(timeout=frames / 50 + 120)
    if lobby:
        lobby.terminate()
        lobby.wait(timeout=10)
    for side in 'AB':                                       # keep each scenario's log (and screenshots) under its own name
        out = base / side
        if out.exists():
            shutil.rmtree(out)
        out.mkdir()
        for f in [run_dir / side / 'tc2.log', run_dir / side / 'voice_out.wav', *(run_dir / side).glob('cap-*.ppm')]:
            if f.exists():
                shutil.move(str(f), out / f.name)
    logs = {side: (base / side / 'tc2.log').read_text(errors='replace') for side in 'AB'}
    rows = {side: [tuple(int(v) if v.lstrip('-').isdigit() else v for v in m.groups()) for m in STATUS.finditer(log)] for side, log in logs.items()}
    failures = []
    for side, log in logs.items():
        if not re.search(r' 0 traps, 0 FP exceptions', log):
            failures.append(f'{side}: CPU traps or FP exceptions')
        if 'GO: session' not in log:
            failures.append(f'{side}: never reached a linked session')
    side_of = {side: {r[1] for r in rows[side] if r[2] == 'on'} for side in 'AB'}
    if side_of['A'] != {1} or side_of['B'] != {2}:
        failures.append(f'cabinet assignment: A {side_of["A"]} B {side_of["B"]} (want P1 / P2)')
    for side in 'AB':
        linked = [r for r in rows[side] if r[2] == 'on']
        if not linked:
            continue
        last = linked[-1]
        if last[11]:
            failures.append(f'{side}: the game counted {last[11]} link errors')
        if last[10]:
            failures.append(f'{side}: {last[10]} malformed fragments')
        # a frozen cabinet falls behind real time; its partner reaches the frame limit and quits first, so look before that
        lost = max([int(m) for m in re.findall(r'freeze=(\d+)', script_a + script_b)] or [0]) * 60 // 1000
        tail = [r for r in linked if frames - 900 - lost <= r[0] < frames - lost - 60]
        if check in ('steady', 'rejoin', 'recover', 'late', 'voice', 'mute', 'modescreen') and tail and not all(r[3] for r in tail):
            failures.append(f'{side}: partner not alive near the end')
        if check in ('steady', 'voice'):
            dead = [r[0] for r in linked if r[0] >= 1200 and not r[3]]
            if dead:
                failures.append(f'{side}: link down at frames {dead}')
    if check == 'modescreen':
        if 'SOLO/LINKED taken: hosting a room' not in logs['A']:
            failures.append('A: taking SOLO/LINKED did not host a room')
        if 'SOLO/LINKED shot without a partner' in logs['A']:
            failures.append('A: SOLO/LINKED was crossed out (the stand-in should answer from power-on)')
    if check in ('modescreen', 'dropin'):
        if 'P2 linked after its power-on handshake' in logs['A']:
            failures.append('A: restarted as if it were P2')
        if 'restarts' in logs['A']:   # the stand-in answers from power-on: P1 never restarts
            failures.append('A: restarted')
        if 'P2 linked after its power-on handshake' not in logs['B']:   # B linked long after power-on: one restart to shake hands
            failures.append('B: did not restart for the handshake after linking late')
        if 'drop-in open' not in logs['A']:
            failures.append('A: the stand-in never answered')
    if check in ('dropin', 'modescreen'):
        # A takes LINK PLAY alone, before B ever links (A's first choice frame < A's first session)
        choice = [int(f) for f in re.findall(r'\[RAM\] frame (\d+) 2F3930: [01] -> 4', logs['A'])]
        linked = re.search(r'frame (\d+): the partner replaces the stand-in', logs['A'])
        if not choice or not linked or choice[0] >= int(linked.group(1)):
            failures.append(f'A: LINK PLAY was not taken alone before B linked (choice {choice[:1]}, handover {linked.group(1) if linked else None})')
    if check in ('modescreen', 'p2join', 'dropin'):
        # each cabinet's choice, at the frame it leaves SELECT GAME MODE (0x2CE92B 1 -> 0): 0x2F3930 1 -> 4 is LINK PLAY, 1 -> 2
        # SOLO ONLY (also what the ~15 s timeout picks); a TEST restart leaves with 1 -> 0, and the screen after one waits at 0, not 1.
        # (In a game 0x2F3930 means other things.)
        for side in 'AB':
            left = set(re.findall(r'\[RAM\] frame (\d+) 2CE92B: 1 -> 0', logs[side]))
            choice = {f: v for f, v in re.findall(r'\[RAM\] frame (\d+) 2F3930: [01] -> (\d+)', logs[side]) if f in left}
            if '4' not in choice.values() or '2' in choice.values():
                failures.append(f'{side}: LINK PLAY not taken (mode screen left with {choice or "nothing"})')
    if check == 'rejoin' and logs['A'].count('GO: session') < 2:
        failures.append('A: no second session after B rejoined')
    if name in ROOM_SCENARIOS:
        rooms = set(re.findall(r'in room (\d+) \(code', logs['A']))
        if 'in room' not in logs['A'] or not re.search(r'GO: session', logs['B']):
            failures.append('the room never linked')
        if 'KXQ7' not in logs['A']:
            failures.append('A did not open a room with the code')
    if check == 'voice':
        problem = voice_check(base / 'B' / 'voice_out.wav')
        if problem:
            failures.append('voice A->B: ' + problem)
        b_sent = [r[14] for r in rows['B'] if r[13] == 'on']
        if b_sent and b_sent[-1] > 10:
            failures.append(f'B sent {b_sent[-1]} voice frames from a silent microphone (the voice gate should stay shut)')
    if check == 'duplex':
        for side in 'AB':
            problem = voice_check(base / side / 'voice_out.wav')
            if problem:
                failures.append(f'voice to {side}: ' + problem)
    if check == 'mute':
        received = [(r[0], r[15]) for r in rows['B'] if r[13] == 'on']
        before = [c for f, c in received if f <= 2400]
        after = [c for f, c in received if f >= 2700]
        if not before or before[-1] < 50:
            failures.append('no voice arrived before A muted')
        elif after and after[-1] - after[0] > 0:
            failures.append(f'{after[-1] - after[0]} voice frames arrived after A muted')
    summary = {side: (rows[side][-1] if rows[side] else None) for side in 'AB'}
    print(f'[{name}] ' + ('PASS' if not failures else 'FAIL'), flush=True)
    for side in 'AB':
        r = summary[side]
        if r:
            print(f'    {side}: P{r[1]} link {r[2]} alive {r[3]} lag {r[4]} tx {r[6]} rx {r[7]} dropped {r[8]} ping {r[12]} ms'
                  f' | voice {r[13]} sent {r[14]} received {r[15]} lost {r[16]}', flush=True)
    for f in failures:
        print('    - ' + f, flush=True)
    if not keep:
        for side in 'AB':
            for p in (base / side).glob('cap-*.ppm'):
                p.unlink()
    return not failures


UPSTREAM_GAME = BUILD / 'upstream-release/windows-release/TimeCrisis2.exe'


def crossplay(keep: bool):
    """Our cabinet (P1, hosting a room) against upstream namco22-decompile's own released Time Crisis 2 (P2, headless), through
    upstream's server. Needs build/upstream-release (docs/ONLINE.md, Testing). Checks the link from both sides and upstream's own
    agreed start (it holds its start until our game has run 25 s, reading our trailer)."""
    if not UPSTREAM_GAME.exists():
        print('[crossplay] SKIPPED: no upstream release in build/upstream-release', flush=True)
        return True
    frames = 6600
    base = WORK / 'crossplay'
    base.mkdir(parents=True, exist_ok=True)
    run_dir = WORK / 'cabinets'
    cabinet(run_dir / 'A', f'127.0.0.1:{SERVER_PORT}')
    rec = base / 'inputs.rec'
    recording(rec, frames)
    microphone(run_dir / 'A' / 'mic.wav', frames // 60 + 30, tone=True)
    lobby = subprocess.Popen([str(SERVER_EXE), '--port', str(SERVER_PORT), '--bind', '127.0.0.1', '--quiet'], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(0.5)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', TCVR_TEST_HIDDEN='1', TC2_REPLAY=str(rec), TC2_NET_STATUS='1', TC2_NET_SCRIPT='600:hostroom', TC2_NET_ROOM_CODE='KXQ7',
               TC2_VOICE_IN=str(run_dir / 'A' / 'mic.wav'))
    ours = subprocess.Popen([str(run_dir / 'A' / 'TimeCrisis2VR.exe'), '--desktop', '--no-dialog', '--frames', str(frames)], cwd=run_dir / 'A', env=env)
    time.sleep(16)                                   # upstream joins the server's fullest open room: ours, once it exists
    uenv = dict(os.environ, SDL_AUDIODRIVER='dummy', TC2_NET_SERVER=f'127.0.0.1:{SERVER_PORT}', TC2_NET_NAME='UPSTREAM', TC2_NET_AUTOSTART='2', TC2_LINKLOG='1', TC2_REPLAY=str(rec))
    theirs = subprocess.Popen([str(UPSTREAM_GAME), 'reset', str(frames * 2_900_000)], cwd=UPSTREAM_GAME.parent, env=uenv)
    ours.wait(timeout=frames / 50 + 120)
    theirs.wait(timeout=frames / 50 + 120)
    lobby.terminate(); lobby.wait(timeout=10)
    shutil.copy2(run_dir / 'A' / 'tc2.log', base / 'ours.log')
    shutil.copy2(UPSTREAM_GAME.parent / 'tc2.log', base / 'upstream.log')
    a = (base / 'ours.log').read_text(errors='replace')
    u = (base / 'upstream.log').read_text(errors='replace')
    rows = [tuple(int(v) if v.lstrip('-').isdigit() else v for v in m.groups()) for m in STATUS.finditer(a)]
    states = [int(p) for p, _ in re.findall(r'partner-state (\d+) self (-?\d+)', a)]
    selfs = [int(m) for m in re.findall(r'partner-state \d+ self (-?\d+)', a)]
    failures = []
    if 'GO: session' not in a or 'online: cabinet Right/Blue' not in u:
        failures.append('the two never linked')
    linked = [r for r in rows if r[2] == 'on' and r[0] >= 2400]
    if not linked or not all(r[3] for r in linked):
        failures.append('our cabinet lost its upstream partner')
    if rows and rows[-1][11]:
        failures.append(f'our game counted {rows[-1][11]} link errors')
    if 4 not in selfs:
        failures.append('upstream never saw our cabinet in a game (state 4)')
    if not re.search(r'start HELD \(us', u) or not re.search(r'start free \(us \d+, partner 4\)', u):
        failures.append("upstream's agreed start did not hold and release on our trailer")
    if ' 0 traps, 0 FP exceptions' not in u or ' 0 traps, 0 FP exceptions' not in a:
        failures.append('CPU traps or FP exceptions')
    if not re.search(r'partner-voice 0', a):
        failures.append('an upstream partner was taken for a voice-chat partner')
    print('[crossplay] ' + ('PASS' if not failures else 'FAIL'), flush=True)
    if rows:
        r = rows[-1]
        print(f'    ours (P1): link {r[2]} alive {r[3]} lag {r[4]} game-errors {r[11]}; partner states seen {sorted(set(states))}; upstream sees us as {sorted(set(selfs))}', flush=True)
    for f in failures:
        print('    - ' + f, flush=True)
    return not failures


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    keep = '--keep' in sys.argv
    if not (PACKAGE / 'TimeCrisis2VR.exe').exists():
        sys.exit('Build first: python -X utf8 tools/build_pc.py --rom <timecrs2.zip>')
    names = args or [*SCENARIOS, 'crossplay']
    ok = all([crossplay(keep) if n == 'crossplay' else run(n, keep) for n in names])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
