"""Online twin-cabinet play: two real desktop instances, linked through the built-in host over loopback.

Needs a Windows build (tools/build_pc.py). Each scenario starts cabinet A (hosts) and cabinet B (joins), both booted as P1 so the
session's cabinet assignment is exercised, feeds both the same scripted coins / start / fire (an arcade input recording), and
reads the game's own link state from the [ONLINE] status lines: the C422 driver's alive flag, its error counters and the lag.

    python -X utf8 tests/test_online.py                 # every scenario (9 scenarios, ~14 minutes, real time)
    python -X utf8 tests/test_online.py basic leave     # some
    python -X utf8 tests/test_online.py --keep          # keep screenshots: build/online-test/<scenario>/{A,B}/cap-*.ppm
"""
from pathlib import Path
import math, re, shutil, subprocess, sys, os

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'artifacts/pc/TimeCrisis2VR-PC'
WORK = ROOT / 'build/online-test'
STATUS = re.compile(r'\[ONLINE\] frame (\d+) side P(\d) link (on|off) alive (\d) lag (\d+) state (\d+) tx (\d+) rx (\d+) '
                    r'drop (\d+) bad (\d+)/(\d+) game-errors (\d+) ping (-?\d+)')


def recording(path: Path, frames: int):
    """Coins and start at 1500 / 1700, then the pedal held and the trigger pulsed while the aim sweeps."""
    lines = ['# TC2 session: F frame inputs gunx guny off', 'F 0 0000 457 161 0']
    f = 1500
    for _ in range(8):
        lines += [f'F {f} 0001 457 161 0', f'F {f + 6} 0000 457 161 0']
        f += 12
    lines += ['F 1700 0010 457 161 0', 'F 1706 0000 457 161 0']
    f, k = 1900, 0
    while f < frames:
        x, y = int(457 + 250 * math.sin(k * .37)), int(161 + 80 * math.sin(k * .23))
        # release the pedal now and then to reload
        pedal = '0020' if (k % 40) < 34 else '0000'
        fire = '0030' if pedal == '0020' else '0000'
        lines += [f'F {f} {fire} {x} {y} 0', f'F {f + 4} {pedal} {x} {y} 0']
        f += 8
        k += 1
    path.write_text('\n'.join(lines) + '\n')


SCENARIOS = {
    # name: (frames, A script, B script, extra env, checks)
    'basic':  (4200, '600:host', '620:join=127.0.0.1', {}, 'steady'),
    'badnet': (4200, '600:host', '620:join=127.0.0.1', {'TC2_NET_SIM': '5,150'}, 'steady'),
    'leave':  (4800, '600:host', '620:join=127.0.0.1;2700:leave;3000:join=127.0.0.1', {}, 'rejoin'),
    'freeze': (4200, '600:host', '620:join=127.0.0.1;2700:freeze=3000', {}, 'recover'),
    'late':   (4200, '600:host', '2400:join=127.0.0.1', {}, 'late'),
    'boot':   (3000, '30:host', '40:join=127.0.0.1', {}, 'steady'),
    'discover': (3000, '600:host', '620:discover', {}, 'steady'),
    'longfreeze': (5400, '600:host', '620:join=127.0.0.1;2700:freeze=8000', {}, 'recover'),
    'hostfreeze': (5400, '600:host;2700:freeze=8000', '620:join=127.0.0.1', {}, 'recover'),
}


def cabinet(directory: Path):
    """The two cabinets always run from the same two folders: Windows asks for firewall permission once per executable path,
    and the tests only use loopback, so a fresh path per scenario would only produce prompts. Per-run state is reset."""
    directory.mkdir(parents=True, exist_ok=True)
    for name in ('TimeCrisis2VR.exe', 'openxr_loader.dll'):
        shutil.copy2(PACKAGE / name, directory / name)
    for name in ('extracted', 'models'):
        shutil.copytree(PACKAGE / name, directory / name, dirs_exist_ok=True)
    for stale in [*directory.glob('cap-*.ppm'), *directory.glob('*.log'), *directory.glob('*.rec'), *directory.glob('*.nv')]:
        stale.unlink()
    (directory / 'quest-options.cfg').write_text('player_side=1\n')
    (directory / 'online.cfg').write_text(f'name=CAB{directory.name}\n')


def run(name: str, keep: bool):
    frames, script_a, script_b, extra, check = SCENARIOS[name]
    base = WORK / name
    run_dir = WORK / 'cabinets'
    rec = base / 'inputs.rec'
    base.mkdir(parents=True, exist_ok=True)
    for side in 'AB':
        cabinet(run_dir / side)
    recording(rec, frames)
    procs = []
    for side, script in (('A', script_a), ('B', script_b)):
        env = dict(os.environ, TCVR_TEST_HIDDEN='1', TC2_REPLAY=str(rec), TC2_NET_STATUS='1', TC2_NET_SCRIPT=script, **extra)
        if keep:
            env['TCVR_CAPTURE_EVERY'] = '600'
        procs.append(subprocess.Popen([str(run_dir / side / 'TimeCrisis2VR.exe'), '--desktop', '--no-dialog', '--frames', str(frames)],
                                      cwd=run_dir / side, env=env))
    for p in procs:
        p.wait(timeout=frames / 50 + 120)
    for side in 'AB':                                       # keep each scenario's log (and screenshots) under its own name
        out = base / side
        if out.exists():
            shutil.rmtree(out)
        out.mkdir()
        for f in [run_dir / side / 'tc2.log', *(run_dir / side).glob('cap-*.ppm')]:
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
        if check in ('steady', 'rejoin', 'recover', 'late') and tail and not all(r[3] for r in tail):
            failures.append(f'{side}: partner not alive near the end')
        if check == 'steady':
            dead = [r[0] for r in linked if r[0] >= 1200 and not r[3]]
            if dead:
                failures.append(f'{side}: link down at frames {dead}')
    if check == 'rejoin' and logs['A'].count('GO: session') < 2:
        failures.append('A: no second session after B rejoined')
    summary = {side: (rows[side][-1] if rows[side] else None) for side in 'AB'}
    print(f'[{name}] ' + ('PASS' if not failures else 'FAIL'))
    for side in 'AB':
        r = summary[side]
        if r:
            print(f'    {side}: P{r[1]} link {r[2]} alive {r[3]} lag {r[4]} tx {r[6]} rx {r[7]} dropped {r[8]} ping {r[12]} ms')
    for f in failures:
        print('    - ' + f)
    if not keep:
        for side in 'AB':
            for p in (base / side).glob('cap-*.ppm'):
                p.unlink()
    return not failures


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    keep = '--keep' in sys.argv
    if not (PACKAGE / 'TimeCrisis2VR.exe').exists():
        sys.exit('Build first: python -X utf8 tools/build_pc.py --rom <timecrs2.zip>')
    names = args or list(SCENARIOS)
    ok = all([run(n, keep) for n in names])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
