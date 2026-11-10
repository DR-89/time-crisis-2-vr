"""Online twin-cabinet play: two real desktop instances, linked through the built-in host or a local lobby server.

Needs a Windows build (tools/build_pc.py). Each scenario starts cabinet A (hosts) and cabinet B (joins), both booted as P1 so the
session's cabinet assignment is exercised, feeds both the same scripted coins / start / fire (an arcade input recording), and
reads the game's own link state from the [ONLINE] status lines: the C422 driver's alive flag, its error counters and the lag.
Room scenarios run upstream's current nmn-server (build/nmn-server-head, see docs/ONLINE.md) on 127.0.0.1 only. Microphones are replaced by
WAV files (a synthetic voice for A, tests/speech.py; silence for B), so no real microphone is opened; what B plays back is recorded
and compared with what A said. The microphone's echo cancellation and noise suppression run as in play.

    python -X utf8 tests/test_online.py                 # every scenario (20 scenarios + crossplay, ~40 minutes, real time)
    python -X utf8 tests/test_online.py basic room      # some
    python -X utf8 tests/test_online.py --keep          # keep screenshots: build/online-test/<scenario>/{A,B}/cap-*.ppm
"""
from pathlib import Path
import math, os, re, shutil, subprocess, sys, threading, time, wave
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, str(Path(__file__).resolve().parent))
from speech import speech  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'artifacts/pc/TimeCrisis2VR-PC'
# $TC2_TEST_BUILD: another checkout's build folder (a git worktree reuses the cabinet folders Windows' firewall already allows)
BUILD = Path(os.environ.get('TC2_TEST_BUILD', ROOT / 'build'))
WORK = BUILD / 'online-test'
SERVER_EXE = BUILD / 'nmn-server-head/target/release/nmn-server.exe'   # upstream's current server: it knows Time Crisis 2's 1 KB frames
SERVER_PORT = 27751
STATUS = re.compile(r'\[ONLINE\] frame (\d+) side P(\d) link (on|off|stand-in) alive (\d) lag (\d+) state (\d+) tx (\d+) rx (\d+) '
                    r'drop (\d+) bad (\d+)/(\d+) game-errors (\d+) ping (-?\d+) \| voice (on|off) sent (\d+) received (\d+) lost (\d+)')


def recording(path: Path, frames: int, aim_until: int = 0, coins_at: int = 1500, again_at: int = 0, idle: str = '', aim_again: str = '',
              coins: bool = True, exposed: int = 0, out_of: int = 34, n_coins: int = 8):
    """Coins and start at 1500 / 1700 (or coins_at / +200), then the pedal held and the trigger pulsed while the aim sweeps
    (held on the mode screen's top panel until aim_until); again_at: coins once more (a cabinet that restarted); idle 'a-b': no
    trigger, pedal or aim from frame a to b (a player who lets SELECT GAME MODE run out); exposed: from that frame on, out of cover
    and never firing, so the player loses every life and lets the continue run out; out_of: frames-in-40 out of cover (fewer: a
    careful player who outlives the other); aim_again 'a-b': the aim held on the
    mode screen's top panel again from a to b (a cabinet that restarted and comes back to SOLO/LINKED); coins False: the start,
    trigger and aim at the same times, but no coins (a player who leaves it to "Join with a credit")."""
    events = [(0, '0000', 457, 161)]
    for at in (coins_at, again_at):
        if not at or not coins:
            continue
        for i in range(n_coins):
            events += [(at + 12 * i, '0001', 457, 161), (at + 12 * i + 6, '0000', 457, 161)]
    start = coins_at + max(200, 12 * n_coins + 20)
    events += [(start, '0010', 457, 161), (start + 6, '0000', 457, 161)]
    f, k = start + 200, 0
    again = tuple(int(v) for v in aim_again.split('-')) if aim_again else (0, 0)
    while f < frames:
        held = f < aim_until or again[0] <= f < again[1]
        x, y = (457, 161) if held else (int(457 + 250 * math.sin(k * .37)), int(161 + 80 * math.sin(k * .23)))
        # release the pedal now and then to reload
        pedal = '0020' if (k % 40) < out_of else '0000'
        fire = '0030' if pedal == '0020' else '0000'
        events += [(f, fire, x, y), (f + 4, pedal, x, y)]
        f += 8
        k += 1
    events = [e for e in events if not (again_at and again_at <= e[0] < again_at + 96 and e[1] not in ('0001', '0000'))]
    if idle:
        a, b = (int(v) for v in idle.split('-'))
        events = [e for e in events if not (a <= e[0] < b + 300 and e[1] not in ('0001', '0000'))]
        # then the trigger alone: one pull to come back, one to choose (a pedal held as the game starts opens GUN INITIALIZATION)
        events += [(b, '0010', 457, 161), (b + 6, '0000', 457, 161), (b + 150, '0010', 457, 161), (b + 156, '0000', 457, 161)]
    if exposed:
        events = [e for e in events if e[0] < exposed] + [(exposed, '0020', 457, 161)]
    events.sort(key=lambda e: e[0])                       # the replay reads in order
    lines = ['# TC2 session: F frame inputs gunx guny off'] + [f'F {f} {i} {x} {y} 0' for f, i, x, y in events]
    path.write_text('\n'.join(lines) + '\n')


def microphone(path: Path, seconds: int, voice: str = ''):
    """48 kHz mono: a synthetic voice ('near' or 'far', tests/speech.py) saying its lines over and over, a second's pause between
    (or silence). Tones would not do: noise suppression takes them out, as it should."""
    rate = 48000
    clip = b''
    if voice:
        with wave.open(str(speech(voice))) as w:
            clip = w.readframes(w.getnframes()) + bytes(2 * rate)
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        want = seconds * rate * 2
        w.writeframes((clip * (want // len(clip) + 1))[:want] if clip else bytes(want))


def _bands(pcm):
    """The long-term spectrum of the voiced 40 ms frames, in 16 bands from 200 Hz to 7 kHz (log power); None: too little voice."""
    import numpy
    f = 1920
    x = pcm[:len(pcm) // f * f].reshape(-1, f)
    x = x[numpy.sqrt((x ** 2).mean(axis=1)) > 300]
    if len(x) < 12:
        return None
    power = numpy.abs(numpy.fft.rfft(x * numpy.hanning(f), axis=1)) ** 2
    freqs = numpy.fft.rfftfreq(f, 1 / 48000)
    edges = numpy.geomspace(200, 7000, 17)
    return numpy.log10(numpy.array([power[:, (freqs >= a) & (freqs < b)].mean() for a, b in zip(edges[:-1], edges[1:])]) + 1e-9)


def voice_check(path: Path, voice: str = 'near'):
    """What a cabinet played back: over a second of voice, loud enough, and with the spectrum of what the partner said."""
    import numpy
    if not path.exists() or path.stat().st_size <= 44:
        return 'no voice was played back'
    with wave.open(str(path)) as w:
        pcm = numpy.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(numpy.float64)
    heard = _bands(pcm)
    if heard is None:
        return f'only {len(pcm) / 48000:.2f} s played back, too little of it voice'
    loud = numpy.sqrt(numpy.mean(pcm[numpy.abs(pcm) > 0] ** 2)) if numpy.any(pcm) else 0
    if loud < 500:
        return f'played back too quietly (RMS {loud:.0f})'
    with wave.open(str(speech(voice))) as w:
        said = _bands(numpy.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(numpy.float64))
    alike = float(numpy.corrcoef(heard, said)[0, 1])
    if alike < 0.9:
        return f'what was played back does not sound like what was said (spectra {alike:.2f} alike)'
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
    # push to talk: A talks all along but holds the key only from 1500 to 2400; B hears it then and only then
    'ptt':    (3600, '600:hostroom;1500:talk=1;2400:talk=0', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'A_CFG': 'push_to_talk=1'}, 'ptt'),
    # echo: B's microphone hears B's own speaker (the game, and A's voice) 60 ms late at 0.8; B says nothing. The echo canceller
    # takes the speaker out once it has learned the room: measured, 85 frames of A's voice went back to A in the first 3 s of the
    # session and none after; without the canceller (TC2_VOICE_DSP=2 on B) 1514, all along
    'echo':   (3600, '600:hostroom', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'B_ENV': 'TC2_VOICE_ECHO=0.8,60'}, 'echo'),
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
    # nobody chooses: A, credited, lets SELECT GAME MODE run out. Instead of the game choosing, A restarts into its intro with its
    # 8 coins kept; A's trigger at 3000 pays them back, the screen comes up again and A takes SOLO/LINKED (a room opens). B joins by
    # code a while into A's game and takes LINK PLAY. menulinked: the same with B linked from the start (a real partner, not the
    # stand-in, while A restarts)
    'menutimeout': (8400, '', '3600:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502,2CE7B7',
                    'A_IDLE': '1600-3000', 'AIM_UNTIL': 3400, 'B_COINS_AT': 4800, 'B_AIM_UNTIL': 7400}, 'menutimeout'),
    'menulinked': (8400, '600:hostroom', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502,2CE7B7',
                   'A_IDLE': '1600-3000', 'AIM_UNTIL': 3400, 'B_COINS_AT': 4800, 'B_AIM_UNTIL': 7400}, 'menutimeout'),
    # Coming back (REJOIN). The server gives a returning player the lowest free slot, and the partner who stayed reconnects to
    # reopen the room, so slots no longer tell the sides apart: the cabinets agree (tc2_online.c, side block). p1return: the
    # drop-in game above, then A (P1, the room's host) restarts out of it (RESTART & LEAVE) at 8000 while B plays on, rejoins at
    # 8300 as P1 again, puts coins in at 9000 and takes LINK PLAY: a game of its own, as a switched-off left cabinet would, while
    # B's goes on. p2return: B restarts out and rejoins as P2, puts coins in and joins A's game again.
    'p1return': (13800, '600:hostroom;8000:restart;8300:rejoin', '3600:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7',
                 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502,2F3FD3', 'B_COINS_AT': 4800, 'B_AIM_UNTIL': 7400, 'A_COINS_AGAIN': 9000,
                 'A_AIM_AGAIN': '9000-10800'}, 'return'),
    # (B comes back while A's game is young: A plays on random inputs, and a P2 can only drop into a game still running)
    'p2return': (9000, '600:hostroom', '3600:joincode=KXQ7;4000:restart;4300:rejoin', {'TC2_NET_ROOM_CODE': 'KXQ7',
                 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502,2F3FD3', 'B_COINS_AT': 5300, 'B_AIM_UNTIL': 7600}, 'return'),
    # a newcomer where P2 stayed: A leaves for good at 6000 and joins the room afresh by its code at 6300; the server gives A
    # slot 1, B (who stayed, P2) keeps P2 and A takes P1
    'newcomer': (9000, '600:hostroom;6000:leave;6300:joincode=KXQ7', '3600:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7',
                 'TCVR_RAMWATCH': '2F3502,2F3FD3', 'B_COINS_AT': 4800, 'B_AIM_UNTIL': 7400}, 'return'),
    # REJOIN after an app restart: A starts with online.cfg remembering room 1 as P1. B opens room 1 (slot 0, so the slot alone
    # would make B P1); A rejoins and asks for P1, B has no claim and takes P2
    'persist': (4200, '900:rejoin', '600:hostroom', {'TC2_NET_ROOM_CODE': 'KXQ7', 'A_REJOIN': 'rejoin=1 KXQ7 1 {now} CABB'}, 'persist'),
    'dropin': (8400, '600:hostroom', '3600:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502',
               'B_COINS_AT': 4800, 'B_AIM_UNTIL': 7400}, 'dropin'),
    # "Join with a credit": the drop-in, but B never puts a coin in. Once its handshake restart is done and A's game runs, one
    # credit goes in for B, and B takes LINK PLAY with it
    'credit': (8400, '600:hostroom', '3600:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502,2CE7B7',
               'B_COINS_AT': 4800, 'B_AIM_UNTIL': 7400, 'B_NO_COINS': '1', 'B_CFG': 'join_credit=1'}, 'dropin'),
    # P1's game ends (A loses every life and lets the continue run out) while P2's goes on, and the lobby server's session limit
    # (1 minute here) comes round meanwhile: as in the arcade, P2 plays on
    'p1over': (22200, '600:hostroom', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502,2F3FD3',
               'AIM_UNTIL': 2600, 'B_COINS_AT': 3000, 'B_AIM_UNTIL': 5000, 'B_IDLE': '3700-30000', 'A_COINS_N': 4,
               'SERVER_ARGS': '--max-session-min 1'}, 'p1over'),
    # P1 stops (one credit, its game over) and P2 plays on: P2 hides until 14000, then fights, dies and continues with its own
    # coins, as at the arcade (before, P2's trigger and coins were held while P1 was not in a game, so P2 could not continue)
    'p2continue': (26400, '600:hostroom', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502,2F3FD3,2CE7B7',
                   'AIM_UNTIL': 2600, 'B_COINS_AT': 3000, 'B_AIM_UNTIL': 5000, 'B_IDLE': '3900-14000', 'A_COINS_N': 4, 'B_COINS_N': 24},
                   'p2continue'),
    # the lobby server's session limit (15 minutes on the public server) ends the session under a linked game: here 1 minute, with
    # both cabinets in LINK PLAY (the p2join game). The game must go on for both, as if nothing happened
    'sessionlimit': (7800, '600:hostroom', '700:joincode=KXQ7', {'TC2_NET_ROOM_CODE': 'KXQ7', 'TCVR_RAMWATCH': '2CE92B,2F3930,2F3502,2F3FD3',
                     'AIM_UNTIL': 2600, 'B_COINS_AT': 3000, 'B_AIM_UNTIL': 5000, 'SERVER_ARGS': '--max-session-min 1'}, 'sessionlimit'),
}
ROOM_SCENARIOS = {'room', 'browse', 'mute', 'voicenet', 'duplex', 'roomleave', 'modescreen', 'p2join', 'dropin', 'menutimeout', 'menulinked',
                  'p1return', 'p2return', 'newcomer', 'persist', 'ptt', 'echo', 'credit', 'sessionlimit', 'p1over', 'p2continue'}


# Lanes, so the scenarios run side by side: lane 0 is the cabinet folders A / B and lobby port 27751 (the LAN scenarios all run
# there, one after another: the built-in host's UDP 27750 and LAN discovery are fixed); room lanes 1, 2, 3 have their own
# folders (A1 / B1, ...) and lobby ports. Windows may ask once to allow each new folder's copy of the game: loopback only.
LANES = 4
_PRINT = threading.Lock()
_LINES = threading.local()


def report(*args, **kwargs):
    """Scenario output, kept per thread and printed whole when the scenario ends (parallel lanes do not interleave)."""
    kwargs.pop('flush', None)
    if not hasattr(_LINES, 'buf'):
        _LINES.buf = []
    _LINES.buf.append(' '.join(str(a) for a in args))


def flush_report():
    with _PRINT:
        for line in getattr(_LINES, 'buf', []):
            print(line, flush=True)
    _LINES.buf = []


def lane_cabinets(run_dir: Path, lane: int):
    suffix = '' if lane == 0 else str(lane)
    return {'A': run_dir / ('A' + suffix), 'B': run_dir / ('B' + suffix)}, SERVER_PORT + 10 * lane


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
    # "Join with a credit" off: the scenarios put their coins in themselves (the credit scenario turns it on for B)
    (directory / 'online.cfg').write_text(f'name=CAB{directory.name}\nserver={server}\njoin_credit=0\n')


def run(name: str, keep: bool, lane: int = 0):
    frames, script_a, script_b, extra, check = SCENARIOS[name]
    extra = dict(extra)
    base = WORK / name
    run_dir = WORK / 'cabinets'
    cab, port = lane_cabinets(run_dir, lane)
    rec = base / 'inputs.rec'
    base.mkdir(parents=True, exist_ok=True)
    server = f'127.0.0.1:{port}' if name in ROOM_SCENARIOS else 'zonesync.invalid'
    for side in 'AB':
        cabinet(cab[side], server)
    recording(rec, frames, int(extra.pop('AIM_UNTIL', 0)), again_at=int(extra.pop('A_COINS_AGAIN', 0)), idle=extra.pop('A_IDLE', ''),
              aim_again=extra.pop('A_AIM_AGAIN', ''), exposed=int(extra.pop('A_EXPOSED', 0)), coins=not extra.pop('A_NO_COINS', ''),
              n_coins=int(extra.pop('A_COINS_N', 8)))
    recs = {'A': rec, 'B': rec}
    if 'B_COINS_AT' in extra:                                # B's own inputs: it puts coins in later
        recs['B'] = base / 'inputs_b.rec'
        recording(recs['B'], frames, int(extra.pop('B_AIM_UNTIL', 0)), int(extra.pop('B_COINS_AT')),
                  again_at=int(extra.pop('B_COINS_AGAIN', 0)), aim_again=extra.pop('B_AIM_AGAIN', ''), coins=not extra.pop('B_NO_COINS', ''),
                  out_of=int(extra.pop('B_OUT_OF', 34)), n_coins=int(extra.pop('B_COINS_N', 8)), idle=extra.pop('B_IDLE', ''))
    if 'A_REJOIN' in extra:                                  # A remembers a room in online.cfg, as after an app restart
        with open(cab['A'] / 'online.cfg', 'a') as f:
            f.write(extra.pop('A_REJOIN').replace('{now}', str(int(time.time()))) + '\n')
    for side in 'AB':                                        # more online.cfg lines (push to talk, a credit to join with)
        if f'{side}_CFG' in extra:
            with open(cab[side] / 'online.cfg', 'a') as f:
                f.write(extra.pop(f'{side}_CFG') + '\n')
    server_args = extra.pop('SERVER_ARGS', '').split()
    side_env = {side: dict(kv.split('=', 1) for kv in extra.pop(f'{side}_ENV').split(';')) if f'{side}_ENV' in extra else {} for side in 'AB'}
    seconds = frames // 60 + 30
    microphone(cab['A'] / 'mic.wav', seconds, 'near')
    microphone(cab['B'] / 'mic.wav', seconds, 'far' if check == 'duplex' else '')   # both talking at once: the echo safeguard must not mute either
    lobby = None
    if name in ROOM_SCENARIOS:
        if not SERVER_EXE.exists():
            sys.exit("Build upstream's current lobby server first (see docs/ONLINE.md, Testing)")
        lobby = subprocess.Popen([str(SERVER_EXE), '--port', str(port), '--bind', '127.0.0.1', '--max-per-ip', '16', '--quiet', *server_args],
                                 stdout=open(base / 'server.log', 'w'), stderr=subprocess.STDOUT)
        time.sleep(0.5)
    procs = []
    for side, script in (('A', script_a), ('B', script_b)):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', TCVR_TEST_HIDDEN='1', TC2_REPLAY=str(recs[side]), TC2_NET_STATUS='1', TC2_NET_SCRIPT=script,
                   TC2_VOICE_IN=str(cab[side] / 'mic.wav'), TC2_VOICE_OUT=str(cab[side] / 'voice_out.wav'), **extra, **side_env[side])
        if keep:
            env.setdefault('TCVR_CAPTURE_EVERY', '600')
        procs.append(subprocess.Popen([str(cab[side] / 'TimeCrisis2VR.exe'), '--desktop', '--no-dialog', '--frames', str(frames)],
                                      cwd=cab[side], env=env))
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
        for f in [cab[side] / 'tc2.log', cab[side] / 'voice_out.wav', *(cab[side]).glob('cap-*.ppm')]:
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
        if check in ('steady', 'rejoin', 'recover', 'late', 'voice', 'echo', 'mute', 'modescreen', 'menutimeout') and tail and not all(r[3] for r in tail):
            failures.append(f'{side}: partner not alive near the end')
        if check in ('steady', 'voice', 'echo'):
            dead = [r[0] for r in linked if r[0] >= 1200 and not r[3]]
            if dead:
                failures.append(f'{side}: link down at frames {dead}')
    if check == 'modescreen':
        if 'SOLO/LINKED taken: hosting a room' not in logs['A']:
            failures.append('A: taking SOLO/LINKED did not host a room')
        if 'SOLO/LINKED shot without a partner' in logs['A']:
            failures.append('A: SOLO/LINKED was crossed out (the stand-in should answer from power-on)')
    if check == 'menutimeout':
        ran_out = re.search(r'frame (\d+): SELECT GAME MODE ran out: back to the intro \(TEST\), 8 coins kept', logs['A'])
        paid = re.search(r'frame (\d+): trigger: 8 coins paid back', logs['A'])
        if not ran_out:
            failures.append('A: SELECT GAME MODE running out did not send it back to the intro with 8 coins kept')
        elif not paid or not 2990 <= int(paid.group(1)) <= 3100:
            failures.append(f'A: the trigger at 3000 did not pay the coins back ({paid.group(1) if paid else None})')
        if logs['A'].count('ran out') != 1:
            failures.append('A: the screen ran out more than once')
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
    if check in ('modescreen', 'p2join', 'dropin', 'menutimeout'):
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
    for side in 'AB':                                        # a cabinet never changes sides within an episode
        if 'the lobby moved this cabinet' in logs[side]:
            failures.append(f'{side}: changed sides mid-session')
    if check in ('return', 'persist'):
        for side, want in (('A', 1), ('B', 2)):
            agreed = re.findall(r'sides agreed: P(\d)', logs[side])
            if not agreed or any(int(a) != want for a in agreed):
                failures.append(f'{side}: sides agreed {agreed}, want P{want} every time')
        if check == 'return' and (logs['A'].count('GO: session') < 2 or logs['B'].count('GO: session') < 2):
            failures.append('no second session after coming back')
    if check == 'persist' and 'rejoining room 1 (KXQ7), asking for P1' not in logs['A']:
        failures.append('A: the remembered room was not rejoined as P1')
    if name == 'p2return':
        # B, back after its restart, takes LINK PLAY again on the right (its second mode-screen choice)
        left = set(re.findall(r'\[RAM\] frame (\d+) 2CE92B: 1 -> 0', logs['B']))
        choices = [v for f, v in re.findall(r'\[RAM\] frame (\d+) 2F3930: [01] -> (\d+)', logs['B']) if f in left and int(f) > 4300]
        if '4' not in choices:
            failures.append(f'B: did not take LINK PLAY after coming back (choices {choices})')
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
    if check == 'echo':
        problem = voice_check(base / 'B' / 'voice_out.wav')
        if problem:
            failures.append('voice A->B: ' + problem)
        sent = [(r[0], r[14]) for r in rows['B'] if r[13] == 'on']
        settled = [c for f, c in sent if sent and f >= sent[0][0] + 600]
        if settled and settled[-1] > settled[0]:
            failures.append(f'B sent {settled[-1] - settled[0]} frames of its own speaker echo after the canceller had 10 s to learn the room')
        if sent and sent[-1][1] > 150:
            failures.append(f'B sent {sent[-1][1]} frames of its own speaker echo in all (want under 150; ~1500 without the canceller)')
    if check == 'duplex':
        for side in 'AB':
            problem = voice_check(base / side / 'voice_out.wav', 'far' if side == 'A' else 'near')
            if problem:
                failures.append(f'voice to {side}: ' + problem)
    if check == 'ptt':
        # B's status every 300 frames: nothing before A holds the key at 1500, voice while it is held, nothing new after 2400
        # (but the 200 ms after it is let go, and what was on the way)
        received = [(r[0], r[15]) for r in rows['B'] if r[13] == 'on']
        before = [c for f, c in received if f <= 1500]
        held = [c for f, c in received if f <= 2400]
        after = [c for f, c in received if f >= 2700]
        if before and before[-1] > 2:
            failures.append(f'{before[-1]} voice frames arrived before A held the talk key')
        if not held or held[-1] - (before[-1] if before else 0) < 25:
            failures.append('too little voice arrived while A held the talk key')
        if after and after[-1] - after[0] > 0:
            failures.append(f'{after[-1] - after[0]} voice frames arrived after A let go of the talk key')
        if 'credit to join with' in logs['B']:
            failures.append('B got a credit with "Join with a credit" off')
    if check == 'p1over':
        # A's game over (0x2F3FD3 1 -> 0) while B's runs, then a session end: the stand-in stays out and B plays on
        over = {side: [int(f) for f in re.findall(r'\[RAM\] frame (\d+) 2F3FD3: 1 -> 0', logs[side])] for side in 'AB'}
        ends = [int(f) for f in re.findall(r'frame (\d+): session in slot \d: keeping', logs['A'])]
        a_over, b_over = (over['A'] or [None])[0], (over['B'] or [frames])[0]
        if a_over is None or not any(a_over < e < b_over for e in ends):
            failures.append(f'the case did not come up: A over at {a_over}, B over at {b_over}, sessions ended at {ends}')
        if logs['A'].count('drop-in open') > 1:
            failures.append('A: the stand-in took the place of a partner still in the room')
        if 'restarts' in logs['B'] or len(re.findall(r'2F3FD3: \d+ -> 2', logs['B'])) != 1:
            failures.append('B: its game did not run on to its own end')
    if check == 'p2continue':
        # A's game over, then B's credits going down (a continue: 0x2CE7B7, coins held) while its game runs on
        a_over = [int(f) for f in re.findall(r'\[RAM\] frame (\d+) 2F3FD3: 1 -> 0', logs['A'])]
        b_over = [int(f) for f in re.findall(r'\[RAM\] frame (\d+) 2F3FD3: 1 -> 0', logs['B'])]
        spent = [(int(f), int(a), int(b)) for f, a, b in re.findall(r'\[RAM\] frame (\d+) 2CE7B7: (\d+) -> (\d+)', logs['B'])]
        cont = [f for f, a, b in spent if b < a and a_over and f > a_over[0] and (not b_over or f < b_over[0])]
        if not a_over:
            failures.append('the case did not come up: A never finished its game')
        elif not cont:
            failures.append(f'B did not continue after A stopped (A over at {a_over[0]}, B over at {b_over[:1]}, B credits {spent[-6:]})')
    if name == 'credit':
        got = re.search(r'frame (\d+): a credit to join with: (\d+) coins in', logs['B'])
        if not got:
            failures.append('B was not given a credit to join with')
        if logs['B'].count('a credit to join with') > 1:
            failures.append('B was given more than one credit')
    if check == 'mute':
        received = [(r[0], r[15]) for r in rows['B'] if r[13] == 'on']
        before = [c for f, c in received if f <= 2400]
        after = [c for f, c in received if f >= 2700]
        if not before or before[-1] < 50:
            failures.append('no voice arrived before A muted')
        elif after and after[-1] - after[0] > 0:
            failures.append(f'{after[-1] - after[0]} voice frames arrived after A muted')
    summary = {side: (rows[side][-1] if rows[side] else None) for side in 'AB'}
    report(f'[{name}] ' + ('PASS' if not failures else 'FAIL'), flush=True)
    for side in 'AB':
        r = summary[side]
        if r:
            report(f'    {side}: P{r[1]} link {r[2]} alive {r[3]} lag {r[4]} tx {r[6]} rx {r[7]} dropped {r[8]} ping {r[12]} ms'
                  f' | voice {r[13]} sent {r[14]} received {r[15]} lost {r[16]}', flush=True)
    for f in failures:
        report('    - ' + f, flush=True)
    if not keep:
        for side in 'AB':
            for p in (base / side).glob('cap-*.ppm'):
                p.unlink()
    return not failures


UPSTREAM_GAME = BUILD / 'upstream-release/windows-release/TimeCrisis2.exe'


def crossplay(keep: bool, lane: int = 0):
    """Our cabinet (P1, hosting a room) against upstream namco22-decompile's own released Time Crisis 2 (P2, headless), through
    upstream's server. Needs build/upstream-release (docs/ONLINE.md, Testing). Checks the link from both sides and upstream's own
    agreed start (it holds its start until our game has run 25 s, reading our trailer)."""
    if not UPSTREAM_GAME.exists():
        report('[crossplay] SKIPPED: no upstream release in build/upstream-release', flush=True)
        return True
    frames = 6600
    base = WORK / 'crossplay'
    base.mkdir(parents=True, exist_ok=True)
    run_dir = WORK / 'cabinets'
    cab, port = lane_cabinets(run_dir, lane)
    cabinet(cab['A'], f'127.0.0.1:{port}')
    rec = base / 'inputs.rec'
    recording(rec, frames)
    microphone(cab['A'] / 'mic.wav', frames // 60 + 30, 'near')
    lobby = subprocess.Popen([str(SERVER_EXE), '--port', str(port), '--bind', '127.0.0.1', '--quiet'], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(0.5)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', TCVR_TEST_HIDDEN='1', TC2_REPLAY=str(rec), TC2_NET_STATUS='1', TC2_NET_SCRIPT='600:hostroom', TC2_NET_ROOM_CODE='KXQ7',
               TC2_VOICE_IN=str(cab['A'] / 'mic.wav'))
    ours = subprocess.Popen([str(cab['A'] / 'TimeCrisis2VR.exe'), '--desktop', '--no-dialog', '--frames', str(frames)], cwd=cab['A'], env=env)
    time.sleep(16)                                   # upstream joins the server's fullest open room: ours, once it exists
    uenv = dict(os.environ, SDL_AUDIODRIVER='dummy', TC2_NET_SERVER=f'127.0.0.1:{port}', TC2_NET_NAME='UPSTREAM', TC2_NET_AUTOSTART='2', TC2_LINKLOG='1', TC2_REPLAY=str(rec))
    theirs = subprocess.Popen([str(UPSTREAM_GAME), 'reset', str(frames * 2_900_000)], cwd=UPSTREAM_GAME.parent, env=uenv)
    ours.wait(timeout=frames / 50 + 120)
    theirs.wait(timeout=frames / 50 + 120)
    lobby.terminate(); lobby.wait(timeout=10)
    shutil.copy2(cab['A'] / 'tc2.log', base / 'ours.log')
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
    report('[crossplay] ' + ('PASS' if not failures else 'FAIL'), flush=True)
    if rows:
        r = rows[-1]
        report(f'    ours (P1): link {r[2]} alive {r[3]} lag {r[4]} game-errors {r[11]}; partner states seen {sorted(set(states))}; upstream sees us as {sorted(set(selfs))}', flush=True)
    for f in failures:
        report('    - ' + f, flush=True)
    return not failures


# --quick: one of each kind (a LAN link, a room with voice, the drop-in, coming back, a newcomer, a credit to join with,
# upstream's desktop game)
QUICK = ['basic', 'room', 'dropin', 'p1return', 'newcomer', 'credit', 'crossplay']


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    keep = '--keep' in sys.argv
    if not (PACKAGE / 'TimeCrisis2VR.exe').exists():
        sys.exit('Build first: python -X utf8 tools/build_pc.py --rom <timecrs2.zip>')
    names = args or (QUICK if '--quick' in sys.argv else [*SCENARIOS, 'crossplay'])
    lanes = 1 if '--serial' in sys.argv else LANES
    # LAN scenarios share lane 0 in turn; room scenarios (and crossplay) take the other lanes, the longest first
    lan = [n for n in names if n != 'crossplay' and n not in ROOM_SCENARIOS]
    rooms = sorted([n for n in names if n not in lan], key=lambda n: -(SCENARIOS[n][0] if n in SCENARIOS else 9000))
    queues = [lan] + [[] for _ in range(max(lanes - 1, 0))]
    if lanes == 1:
        queues[0] += rooms
    else:
        load = [0] * (lanes - 1)
        for n in rooms:                                     # the least loaded room lane takes the next scenario
            i = load.index(min(load))
            queues[i + 1].append(n)
            load[i] += SCENARIOS[n][0] if n in SCENARIOS else 9000

    def lane_run(lane: int, queue: list):
        time.sleep(15 * lane)                              # eight cabinets booting at once starve each other: stagger the lanes
        results = []
        for n in queue:
            try:
                results.append(crossplay(keep, lane) if n == 'crossplay' else run(n, keep, lane))
            except Exception as e:                         # one scenario's crash fails it, not the other lanes
                report(f'[{n}] FAIL\n    - {type(e).__name__}: {e}')
                results.append(False)
            flush_report()
        return results

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=lanes) as pool:
        futures = [pool.submit(lane_run, lane, q) for lane, q in enumerate(queues) if q]
        ok = all(all(f.result()) for f in futures)
    print(f'{len(names)} scenarios in {(time.time() - t0) / 60:.1f} minutes on {sum(1 for q in queues if q)} lanes: ' + ('PASS' if ok else 'FAIL'), flush=True)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
