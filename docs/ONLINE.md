# Online 2-player

Time Crisis II was built as a **twin cabinet**: two complete arcade boards, one per player, joined by a cable. Each board runs its own copy of the game and draws its own player's view; the cable keeps the two games in step. Keith (P1, left/red) and Robert (P2, right/blue) fight through the same stages from their own positions, see each other in the scene, and keep separate scores.

This build emulates that cable, the System 23 **C422** link chip, and carries it over the internet or your home network. Both players run the full game, so there is no "host advantage", no rollback and no desync: it is the arcade's own two-cabinet mode, as Namco built it.

Quest, Windows PCVR and the Windows desktop mode all play with each other.

## Quick start

1. Both players install **the same build** of Time Crisis II VR Online.
2. Press the **left menu button** to open options, then **left stick up / down** to reach page **4/4 ONLINE 2-PLAYER**.
3. One player chooses **HOST A GAME**. The page lists their addresses.
4. The other player joins in one of these ways:
   - **FIND LAN GAMES** on the same home network, then select the host.
   - **JOIN IP** for an address over the internet; see [Playing over the internet](#playing-over-the-internet).
   - **QUICK MATCH** pairs you with anyone waiting on the public server.
5. The games link automatically: the host becomes **P1 Keith**, the joiner **P2 Robert**. A banner confirms it: `LINKED WITH <NAME> - P2 ROBERT`.
6. Close the menu. Each player inserts credits (**A**) and pulls the **trigger** to start, just like at the arcade. Link first, then start together: a partner can also link while you are already playing (tested), but how the arcade merges two games already in progress has not been checked.

| ONLINE page control | Quest | Desktop |
| --- | --- | --- |
| Move through the menu | Right stick up / down | Up / Down |
| Select | Either trigger | Enter |
| Edit an address or name (in the field) | Right stick: digit/letter up / down, left / right: move | Arrows |
| Back / cancel editing | Left stick click | Backspace or Home |
| Change page | Left stick up / down | Tab |

**CHANGE NAME** sets the name your partner sees (letters, digits, `-`, `_`; 12 characters).

## While you are linked

- **The game keeps running while the menu is open, while the headset is off and while the app is in the background.** Your partner's cabinet needs yours, exactly as at the arcade. Your controls are released while you are away. On Quest this holds until the headset goes to sleep; after that the reconnection below takes over.
- **If your partner drops** (Wi-Fi gap, headset asleep, app restarted), the game behaves like a twin cabinet whose other half was switched off. The host reopens the room by itself; a joiner keeps reconnecting to the same address for a minute. When the partner returns, the banner shows `<NAME> IS BACK - LINKED AGAIN`.
- **Leaving:** ONLINE page, **LEAVE ONLINE GAME**. You go back to single-player on your usual cabinet side.
- A banner warns `PARTNER CONNECTION IS LAGGING` when the arcade link has been more than six frames behind for over a second and a half.

## Playing over the internet

There is nothing to pay for and no account. Pick one:

| Method | Setup | Notes |
| --- | --- | --- |
| **Same network** | None. Use **FIND LAN GAMES**. | Best latency. |
| **Tailscale / ZeroTier (free)** | Both players join the same tailnet. The host's ONLINE page shows its address marked `(VPN)`; the joiner uses **JOIN IP** with it. | No router changes. On Quest this needs the Tailscale/ZeroTier Android app sideloaded (not verified here); a PC on the tailnet works. |
| **Port forward** | The host forwards **UDP 27750** on their router to the PC or Quest; the joiner uses **JOIN IP** with the host's public address. | Works anywhere; needs router access. |
| **Quick match** | None. | Uses `zonesync.net`, the free community server of the [namco22-decompile](https://github.com/spacestate1/namco22-decompile) project (the same server Rave Racer and Cyber Sled use). It pairs whoever is waiting. |
| **Your own server** | Run `nmn-server` from `upstream/server` (Rust, standard library only; a `Dockerfile` is included) on any machine with UDP 27750 open, for example a free-tier cloud VM. Put `server=your.host` in `online.cfg`. | Rooms, many games at once, no amplification. |

Traffic is about **18 KB/s each way** (one 256-byte packet per arcade frame, at most three on the few frames with large link packets).

### Latency

The arcade's own link driver declares the partner lost after **16 frames (about 270 ms) without a packet**, then recovers as soon as packets return. Tested on loopback with 5% packet loss plus 150 ms stalls, 3-second and 8-second freezes of either player, a partner leaving and rejoining mid-game, and a partner joining a game already in progress: the game kept its 0 link errors and re-linked every time. Real internet play has not yet been measured; under about 100 ms round trip it should feel like the arcade.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| `NO ANSWER - CHECK ADDRESS / PORT 27750` | Check the host's address on their ONLINE page. Over the internet, the host must forward **UDP** 27750, or both of you must be on the same Tailscale/ZeroTier network. |
| A Windows host is never found | When Windows asks whether `TimeCrisis2VR.exe` may use the network, allow it on **Private networks**. Otherwise add an inbound rule for UDP 27750. |
| **FIND LAN GAMES** finds nothing | Some routers block broadcasts between Wi-Fi clients ("AP/client isolation") or between 2.4 and 5 GHz bands. Use **JOIN IP** with the address shown on the host's page. |
| `CANNOT HOST: PORT 27750 IS IN USE` | Another copy of the game (or an `nmn-server`) is already hosting on this machine. |
| `PARTNER CONNECTION IS LAGGING` | Prefer 5 GHz Wi-Fi or Ethernet for the PC, and close large downloads. The arcade link gives up after 16 silent frames and recovers on its own. |
| Linked with a stranger | **QUICK MATCH** pairs whoever is waiting; use **HOST** / **JOIN IP** to play with a friend. |

Logs: the Quest log (`Export-Quest-Diagnostics.ps1`) and Windows `tc2.log` contain `[ONLINE]`, `[NET]` and `[LINK]` lines.

## online.cfg

Beside the settings file (Quest: the app's files folder; Windows: next to the executable). It is created on first start.

```
name=MRSCO              # shown to your partner
server=zonesync.net     # QUICK MATCH server, host or host:port
join=192.168.1.20:27750 # the last JOIN IP address
```

## How it works

### The link chip

The System 23 board's C422 is an RS-422 serial controller with 16 KB of buffer RAM (`0x06200000`) and eight 16-bit registers (`0x06400000`); its interrupt is CPU IRQ3 (Cause IP5). MAME and the pinned upstream core only stub it, so the cable was dead. The game's own driver, read from the translated program, defines what the chip must do:

| Routine | Role |
| --- | --- |
| `8000BEAC` | C422 interrupt: status bit 1 is receive, bit 2 is transmit; its caller acknowledges with a write to `0x0D000008` |
| `8000BB6C` | Transmit: sends up to 255 words per chunk (register 3 = 3 / 2 around it); register 7 is set once per packet and advanced by the chip |
| `8000BD24` | Receive: scans back from register 6 (the last word written in the 4096-word ring at words `0x1000-0x1FFF`) for the bit-8 trailer `[length hi][length lo]`, then checks the `0x5A` sync byte |
| `8000BF38` | Once a frame from the main loop: validates queued packets (byte sum 0) and builds the next one, sent three times |

Its variables (gp `0x802C7820`) give the link state shown on the ONLINE page: `0x802F3502` alive, `0x802F3504` frames behind the partner (lost above 16), `gp+0x75BC` state (0 linked, 2 lost) and three error counters.

`quest/tc2_link.c` implements the chip. Register 1 is an interrupt mask that the chip sets as it interrupts and the handler clears to re-arm. Register 6 points at the last word received. Received packets enter the ring one per chip tick (about 350 a frame); after a stall only the newest half-ring's worth (2048 words) is kept, because a burst of backlog would wrap the ring over packets the game has queued but not read. **While no session has started the chip is exactly the upstream stub**, so single-player play is bit-identical: the same instruction and interrupt counts and pixel-identical frames as v0.3.3.

The pinned core is not modified. `quest/tc2_sources.cmake` generates copies of four upstream files with one-line hooks: the register reads and writes, the acknowledge, the chip's clock and the RAM accessor. The configure step fails if any hook no longer matches upstream.

### Cabinet side

The lobby slot decides the cabinet: slot 0 is P1, slot 1 is P2. The game reads the TSS I/O cabinet-ID switch continuously, so the side is set when the session starts with no reboot. The single-player Robert-route adapter is not involved. The side is kept until the player leaves, so a dropped and resumed connection never moves a cabinet mid-game.

### Transport

Upstream's NMN2 lobby/relay protocol (`upstream/docs/NETPLAY.md`): the client `upstream/engine/net.c` and the built-in host `upstream/engine/net_host.c`, compiled unchanged. Game id `t2`, link version 1, 2 players, 256-byte frames. That is the size `nmn-server` allows any unlisted game, so stock servers carry it without changes.

Each frame carries one fragment of the newest link packet:

| Byte | Meaning |
| --- | --- |
| 0 | `0xC4` |
| 1 | copies sent (1-15), bit 7 = deflated |
| 2 | packet id |
| 3 | fragment index << 4, fragment count (1-8) |
| 4-5 | blob length |
| 6-255 | fragment data |

The blob is the packet's low bytes. Bit 8 marks only the last word, so it is rebuilt on receipt. Packets longer than one fragment (380-470 bytes during play) are deflated, and any remainder goes out in extra frames the same frame.

`quest/tc2_online.c` runs the session. Joining counts as Ready, and the lower slot starts as soon as both are in. Once linked, the episode keeps the link enabled through session gaps and reconnects as described above. `quest/quest_host.c` keeps the arcade running behind menus and focus loss while linked, and polls the lobby every display frame.

## Testing

```powershell
python -X utf8 tools/build_pc.py --rom timecrs2.zip --jobs 8
python -X utf8 tests/test_online.py            # 9 scenarios, about 14 minutes in real time
python -X utf8 tests/test_handedness.py        # includes the ONLINE page's input routing
```

`tests/test_online.py` starts two real desktop instances, both booted as P1. A hosts and B joins over loopback. Both play the same scripted coins, start and shooting. Each scenario checks the game's own link state: alive, errors, cabinet sides and re-linking.

| Scenario | What happens |
| --- | --- |
| `basic` | A full linked game |
| `boot` | Linked during the power-on self-test, which exercises the C422 interrupt |
| `discover` | B finds A with **FIND LAN GAMES** (broadcast discovery) instead of an address |
| `badnet` | 5% packet loss and 150 ms stalls on both receive sides |
| `leave` | B leaves mid-game and rejoins |
| `freeze` / `longfreeze` | B's whole app stalls for 3 s / 8 s (longer than the 5 s lobby timeout) |
| `hostfreeze` | The host stalls for 8 s, its built-in server with it |
| `late` | B joins a game A has already started |

Test switches, read by `quest/tc2_online.c`:

- `TC2_NET_SCRIPT="600:host;2700:leave;3000:join=1.2.3.4"`: actions `host`, `join=<addr>`, `discover`, `quick`, `leave`, `freeze=<ms>`, run at those arcade frames.
- `TC2_NET_STATUS=1`: a status line every 300 frames.
- `TC2_NET_DEBUG=1`, `TC2_NET_SIM=loss%,stall_ms`: the library's own logging and simulated bad network.
- `TCVR_TEST_MENU=3@60`: open the ONLINE page at desktop frame 60.
- `TCVR_CAPTURE_EVERY=N`: save `cap-<frame>.ppm` every N frames.

## Limitations

- Hardware so far: a Quest 3 hosting and a Windows desktop instance joining over home Wi-Fi linked and played the scripted game. The link was alive throughout, with 0 game errors on the Quest, 16-35 ms ping and 1-2 frames of lag, and the room reopened when the PC left. Nobody wore the headset during that test. Quest-to-Quest, PCVR, and actually playing linked in a headset are not tested yet.
- No voice chat. Use a party app or a phone call.
- **QUICK MATCH** pairs whoever is waiting; for a friend, use **JOIN IP**.
- Servers end a session after 15 minutes. The lobby re-links within about a second and the arcade link is kept up meanwhile, but this path has not been exercised in a full-length game yet.
- Both players need the same build: the link version is checked by the lobby.
