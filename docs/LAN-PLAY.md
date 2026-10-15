# LAN play

Two players, two cabinets, linked over the network the way the arcade machines are. Each player runs their own copy of the game on their own headset (Quest, Steam Frame or PC, in any combination) and sees their own cabinet's view, while the two cabinets exchange the original link data.

## Playing

1. Both players open options and move the left stick up through the pages (Options, Gun Fit / Player, Graphics) to **LAN PLAY**. On the desktop, Tab cycles to it.
2. One player chooses **Host** and becomes the red cabinet (P1).
3. The other chooses **Find a Game** and picks the host, or **Join by IP**. Joining makes them the blue cabinet (P2).
4. Both select **Ready**; the host selects **Start**. Both cabinets restart to power-on as linked cabinets and boot normally, which takes about 20 seconds. Then insert credits and start as usual.

**Leave** returns to solo. Options stay usable during a linked game, but they don't pause it: your input is ignored while they're open and the arcade clock keeps running for the other player. A headset that goes to sleep stops its cabinet, and the other player sees "Partner disconnected".

Joining by IP uses SteamVR's keyboard on the Frame, the system keyboard on Quest, and normal typing or paste on the desktop.

## How it works

- `link/tc2_link.c` emulates the cabinets' C422 link device and carries its records over TCP. The ROM's own link protocol and link-loss handling do the rest.
- Host and discovery use port 32123 (TCP, and UDP for discovery). Find sends a broadcast every second and a unicast pass over the finder's /24 every three seconds, because the Frame's container network can't be relied on for broadcast and some access points drop it. Larger networks need Join by IP.
- Before forwarding any game data, both sides check a versioned identity (`TC2VR-LAN2/core0837a397/timecrs2-tss3verb/port1`). A behaviour-changing link revision must change it. Bad addresses, busy ports, incompatible peers, timeouts and disconnects stay in the lobby as errors you can retry.
- In a linked game the cabinet-ID input follows the link side: host red, joiner blue. Otherwise the saved P1/P2 player side applies, and Robert's solo route is only selected outside linked games.
- Linked games keep separate backup RAM (`tc2_link_red.nv`, `tc2_link_blue.nv`), so solo settings and scores are untouched.

## Tests

- `tests/test_tc2_session.py`: real sockets. Discovery, compatibility, ready and start, reconnects, premature or hostile records, version mismatch and timeouts.
- `tests/test_tc2_link.py`: link DMA, the receive ring and the control queue.
- `tests/test_quest_lobby.py` and `tests/test_openvr_keyboard.py`: the lobby, IP entry and the SteamVR keyboard bridge.
- `tests/run_tc2_session_gate.py` (needs your ROM and the headless build in `headless/`): two real cores play two negotiated linked games and return to solo. It then checks with `tests/check_tc2_session_evidence.py` that the ROM accepted link packets in every game.

## Status

- **Played:** a Steam Frame hosting and a Quest 3 joining, Find a Game across the two, and about three minutes of linked play that stayed in sync.
- **Not yet played:** Windows PC, the SteamVR keyboard, and the Quest system keyboard.
- **Player 2 damage:** in that session player 2 didn't seem to take hits during a short stint. In the two-core simulation player 2 does lose lives to enemy fire, but less often than player 1 early on, so this still needs a longer check in the headset.
