# Online 2-player

Time Crisis II was built as a **twin cabinet**: two complete arcade boards, one per player, joined by a cable. Each board runs its own copy of the game and draws its own player's view; the cable keeps the two games in step. Keith (P1, left/red) and Robert (P2, right/blue) fight through the same stages from their own positions, see each other in the scene, and keep separate scores.

This build emulates that cable, the System 23 **C422** link chip, and carries it over the internet or your home network, with **voice chat** alongside. Both players run the full game, so there is no "host advantage", no rollback and no desync: it is the arcade's own two-cabinet mode, as Namco built it.

Quest, Windows PCVR and the Windows desktop mode all play with each other.

## Quick start

1. Both players install **the same build** of Time Crisis II VR Online.
2. Press the **left menu button** to open options, then **left stick up / down** to reach page **4/4 ONLINE 2-PLAYER**.
3. One player chooses **HOST ONLINE ROOM (CODE)**. The page shows a four-character room code, for example `KXQ7`.
4. The other player chooses **JOIN ROOM CODE** and enters it. Alternatively, **BROWSE GAMES** lists every open room on the server and every game hosted on your local network; select one to join.
5. The games link automatically: the host becomes **P1 Keith**, the joiner **P2 Robert**. A banner confirms it: `LINKED WITH <NAME> - P2 ROBERT`. Voice chat starts at the same moment. On Quest, the first time, allow microphone access.
6. Close the menu. Each player inserts credits (**A**) and pulls the **trigger** to start, just like at the arcade. Link first, then start together: a partner can also link while you are already playing (tested), but how the arcade merges two games already in progress has not been checked.

Room codes go through the lobby server, so they work across the internet with no router setup, VPN or addresses. Other ways to meet:

| Item | What it does |
| --- | --- |
| **HOST ONLINE ROOM (CODE)** | Opens a room on the lobby server and shows its code |
| **JOIN ROOM CODE** | Joins the room with that code |
| **BROWSE GAMES** | Lists the server's rooms (name, players, `JOIN` / `PLAYING` / `FULL` / `OLD` version) and LAN games; refreshes every 4 seconds |
| **HOST ON THIS NETWORK** | The built-in host on UDP 27750; lists your addresses for **JOIN IP** |
| **JOIN IP** | Joins a built-in host by address, on the LAN or over the internet |
| **QUICK MATCH (ANYONE)** | Joins whoever is waiting on the server |

| ONLINE page control | Quest | Desktop |
| --- | --- | --- |
| Move through the menu | Right stick up / down | Up / Down |
| Select | Either trigger | Enter |
| Edit a code, address or name (in the field) | Right stick: character up / down, left / right: move | Arrows |
| Partner volume | Right stick left / right on **PARTNER VOLUME** | Left / Right |
| Back / cancel editing | Left stick click | Backspace or Home |
| Change page | Left stick up / down | Tab |

**CHANGE NAME** sets the name your partner sees (letters, digits, `-`, `_`; 12 characters).

## Voice chat

- **On by default.** It is live only while you are linked to a partner. Turn it off with **VOICE CHAT: ON/OFF** on the ONLINE page.
- **Mute** with a **left stick click** during play, **M** on the desktop, or **MICROPHONE** on the ONLINE page. A banner confirms `MICROPHONE MUTED` / `MICROPHONE ON`.
- **Partner volume** is 0-200%, set with **PARTNER VOLUME**. Your partner's voice is mixed into the game's sound.
- While linked, the ONLINE page shows a level meter for you and for your partner.
- **Open microphone with a voice gate:** only speech is sent, starting above about -42 dBFS and staying open 0.4 s after you stop. Silence costs nothing.
- **Echo:** Quest uses Android's voice-communication input. The Quest 3 offers no separate echo-canceller effect (measured), so the gate also needs four times the level while your partner is speaking. Their voice coming out of your speakers is not sent back to them, while talking over each other still works (tested).
- On Quest, if microphone access is refused, the banner says `NO MICROPHONE ACCESS - LISTEN ONLY`. You still hear your partner. Allow it later in the headset's app permissions.
- Bandwidth is about 3 KB/s while talking: Opus at 20 kbit/s with in-band error correction.

## While you are linked

- **The game keeps running while the menu is open, while the headset is off and while the app is in the background.** Your partner's cabinet needs yours, exactly as at the arcade. Your controls are released while you are away. On Quest this holds until the headset goes to sleep; after that the reconnection below takes over.
- **If your partner drops** (Wi-Fi gap, headset asleep, app restarted), the game behaves like a twin cabinet whose other half was switched off. The host reopens the room by itself, and a joiner keeps reconnecting to the same room or address for a minute. When the partner returns, the banner shows `<NAME> IS BACK - LINKED AGAIN`.
- **Leaving:** ONLINE page, **LEAVE ONLINE GAME**. You go back to single-player on your usual cabinet side.
- A banner warns `PARTNER CONNECTION IS LAGGING` when the arcade link has been more than six frames behind for over a second and a half.

## Playing over the internet

There is nothing to pay for and no account.

| Method | Setup | Notes |
| --- | --- | --- |
| **Room code** (recommended) | None. | The lobby server relays the game, so it works through any home router. Default server `zonesync.net`, the free community server of the [namco22-decompile](https://github.com/spacestate1/namco22-decompile) project (Rave Racer and Cyber Sled use it too). |
| **Same network** | None. Use **BROWSE GAMES** or **HOST ON THIS NETWORK**. | Best latency; no server involved. |
| **Tailscale / ZeroTier (free)** | Both players join the same tailnet. The host's page shows its address marked `(VPN)`; the joiner uses **JOIN IP**. | Direct connection; on Quest this needs the Tailscale/ZeroTier Android app sideloaded (not verified here). |
| **Port forward** | The host forwards **UDP 27750** to the PC or Quest; the joiner uses **JOIN IP** with the host's public address. | Direct connection; needs router access. |
| **Your own server** | Run `nmn-server` from `upstream/server` (Rust, standard library only; a `Dockerfile` is included) on any machine with UDP 27750 open, for example a free-tier cloud VM. Put `server=your.host` in `online.cfg`. | Your own rooms, codes and browser list. |

The game uses about **18 KB/s each way**: one 256-byte packet per arcade frame, at most three on the few frames with large link packets. Voice adds about 3 KB/s while someone talks.

### Latency

The arcade's own link driver declares the partner lost after **16 frames (about 270 ms) without a packet**, then recovers as soon as packets return. Tested on loopback with 5% packet loss plus 150 ms stalls, 3-second and 8-second freezes of either player, a partner leaving and rejoining mid-game, and a partner joining a game already in progress: the game kept its 0 link errors and re-linked every time. Real internet play has not yet been measured; under about 100 ms round trip it should feel like the arcade.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| `NO ROOM WITH THAT CODE` | Check the code on the host's page. Both players must use the same server (`server=` in `online.cfg`). |
| `THAT ROOM RUNS ANOTHER VERSION` / `OLD` in the browser | Both players need the same build of the game. |
| `NO ANSWER - CHECK ADDRESS / PORT 27750` | Check the host's address on their ONLINE page. Over the internet, the host must forward **UDP** 27750, or both of you must be on the same Tailscale/ZeroTier network. Room codes avoid this. |
| A Windows host is never found | When Windows asks whether `TimeCrisis2VR.exe` may use the network, allow it on **Private networks**. Otherwise add an inbound rule for UDP 27750. |
| A LAN game is not listed | Some routers block broadcasts between Wi-Fi clients ("AP/client isolation") or between 2.4 and 5 GHz bands. Use a room code, or **JOIN IP** with the address shown on the host's page. |
| `CANNOT HOST: PORT 27750 IS IN USE` | Another copy of the game (or an `nmn-server`) is already hosting on this machine. |
| `PARTNER CONNECTION IS LAGGING` | Prefer 5 GHz Wi-Fi or Ethernet for the PC, and close large downloads. The arcade link gives up after 16 silent frames and recovers on its own. |
| Your partner hears an echo of themselves | Turn your headset's volume down a little, or wear headphones. The voice gate already suppresses most of it. |
| No voice | Check **VOICE CHAT: ON**, the **MICROPHONE** state, and on Quest the app's microphone permission. On Windows, check that desktop apps may use the microphone (Privacy settings). |
| Linked with a stranger | **QUICK MATCH** pairs whoever is waiting; for a friend, use a room code. |

Logs: the Quest log (`Export-Quest-Diagnostics.ps1`) and Windows `tc2.log` contain `[ONLINE]`, `[NET]`, `[LINK]` and `[VOICE]` lines.

## online.cfg

Beside the settings file (Quest: the app's files folder; Windows: next to the executable). It is created on first start.

```
name=MRSCO              # shown to your partner
server=zonesync.net     # room codes, the browser and QUICK MATCH: host or host:port
join=192.168.1.20:27750 # the last JOIN IP address
voice=1                 # voice chat on
mic_muted=0
voice_volume=100        # the partner's voice, 0-200
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

The pinned core is not modified. `quest/tc2_sources.cmake` generates copies of the upstream files it hooks, and the configure step fails if any hook no longer matches upstream:

- `tc2_chips.c`, `tc2_ctl.c`, `tc2_main.c` and `tc2_mem.c`: the C422 register reads and writes, its acknowledge, its clock and its RAM.
- `tc2_audio.c`: the voice mix.
- `engine/net.c`: a room request for the next connect, and an accessor for the current room.

### Cabinet side

The lobby slot decides the cabinet: slot 0 is P1, slot 1 is P2. The game reads the TSS I/O cabinet-ID switch continuously, so the side is set when the session starts with no reboot. The single-player Robert-route adapter is not involved. The side is kept until the player leaves, so a dropped and resumed connection never moves a cabinet mid-game.

### Transport and rooms

Upstream's NMN2 lobby/relay protocol (`upstream/docs/NETPLAY.md`) carries the game: the client `upstream/engine/net.c` and the built-in host `upstream/engine/net_host.c`. Game id `tv`, link version 2 (the link plus voice), 2 players, 256-byte frames. That is the size `nmn-server` allows any unlisted game, so stock servers carry it without changes.

Upstream namco22-decompile has since added its own Time Crisis II link play (0.9.0 and later) under game id `t2`, with a different wire format and 1 KB frames. The two do not interoperate, and the separate ids keep their rooms apart.

Two kinds of frame share the session:

| Link (`0xC4`) | | Voice (`0xC5`) | |
| --- | --- | --- | --- |
| 1 | copies sent (1-15), bit 7 deflated | 1 | flags (bit 0: talking) |
| 2 | packet id | 2-3 | sequence number of the first Opus frame |
| 3 | fragment index << 4, fragment count (1-8) | 4 | Opus frames in this packet |
| 4-5 | blob length | 5- | per frame: length byte, data |
| 6-255 | fragment data | | |

The link blob is the packet's low bytes. Bit 8 marks only the last word, so it is rebuilt on receipt. Packets longer than one fragment (380-470 bytes during play) are deflated, and any remainder goes out in extra frames the same frame. Each poll sends one frame: link fragments first, then voice when the link has nothing new.

**Rooms.** A hosted room is created with the name `<NAME> <CODE>`. The code uses 26 characters that cannot be misread: no 0/O, 1/I, 2/Z, 5/S or 8/B. **JOIN ROOM CODE** and **BROWSE GAMES** ask the server for this game's rooms without joining any (`quest/tc2_rooms.c`): DISCOVER returns ANNOUNCE, and a ROOMS request padded to 400 bytes returns ROOMLIST, exactly as the server answers any stranger. They then join that room by id. Once inside, every later reconnect asks for the same room.

`quest/tc2_online.c` runs the session. Joining counts as Ready, and the lower slot starts as soon as both are in. Once linked, the episode keeps the link enabled through session gaps and reconnects as described above. `quest/quest_host.c` keeps the arcade running behind menus and focus loss while linked, and polls the lobby every display frame.

### Voice

`quest/tc2_voice.c`:

- **Capture:** 48 kHz mono, in 20 ms frames.
  - Desktop records through an SDL capture device.
  - Quest records in `MainActivity` with `AudioRecord` in `VOICE_COMMUNICATION` mode, plus the platform echo canceller and noise suppressor where the headset provides them.
- **Send:** a level gate, then Opus VOIP at 20 kbit/s with in-band error correction, two frames per network frame.
- **Receive:**
  - A 64-slot jitter store, decoded about 60 ms ahead.
  - A missing frame is rebuilt from the next frame's error correction, or by Opus concealment.
  - The result is resampled to the game's 88.2 kHz and added to both channels of its output, because Android allows one output device.

Opus 1.5.2 is a pinned download (`tools/bootstrap.py`) built into both apps.

## Testing

```powershell
python -X utf8 tools/build_pc.py --rom timecrs2.zip --jobs 8
cd upstream/server; $env:CARGO_TARGET_DIR="../../build/nmn-server"; cargo build --release; cd ../..
python -X utf8 tests/test_online.py            # 15 scenarios, about 20 minutes in real time
python -X utf8 tests/test_handedness.py        # includes the ONLINE page's input routing
```

`tests/test_online.py` starts two real desktop instances, both booted as P1:

- **Running:** A hosts and B joins, over loopback or through a local `nmn-server` bound to 127.0.0.1. Both play the same scripted coins, start and shooting.
- **Microphones:** replaced by WAV files, a 440 Hz tone for A and silence for B. What each side plays back is recorded and checked for that tone.
- **Checks:** the game's own link state (alive, errors, cabinet sides, re-linking) and the voice counts.

| Scenario | What happens |
| --- | --- |
| `basic` | A full linked game |
| `boot` | Linked during the power-on self-test, which exercises the C422 interrupt |
| `discover` | B finds A on the LAN by broadcast instead of an address |
| `badnet` | 5% packet loss and 150 ms stalls on both receive sides |
| `leave` | B leaves mid-game and rejoins |
| `freeze` / `longfreeze` | B's whole app stalls for 3 s / 8 s (longer than the 5 s lobby timeout) |
| `hostfreeze` | The host stalls for 8 s, its built-in server with it |
| `late` | B joins a game A has already started |
| `room` | A hosts a room with a code, B joins by code; A's voice must reach B as 440 Hz, B's silent microphone must send nothing |
| `browse` | B opens the browser and picks A's room |
| `mute` | A mutes mid-game: no voice frame may arrive after it |
| `voicenet` | Voice through 5% loss and stalls (error correction and concealment) |
| `duplex` | Both talk at once: each must hear the other despite the echo safeguard |
| `roomleave` | B leaves the room mid-game and rejoins by code |

Test switches, read by `quest/tc2_online.c` and `quest/tc2_voice.c`:

- `TC2_NET_SCRIPT="600:hostroom;2400:mute"`: actions run at those arcade frames, or `t<ms>:` milliseconds after start.
  - Actions: `host`, `join=<addr>`, `discover`, `quick`, `hostroom`, `joincode=<code>`, `browse`, `pickroom`, `mute`, `voice=0|1`, `leave`, `freeze=<ms>`.
  - On a headset, `online-test.txt` beside the settings (written with `adb run-as` on this debuggable build) holds `script=...`, `status=1` and `voice_out=<file>`.
- `TC2_NET_STATUS=1`: a status line every 300 frames.
- `TC2_NET_ROOM_CODE=KXQ7`: a fixed code for hosted rooms.
- `TC2_VOICE_IN=<wav>`, `TC2_VOICE_OUT=<wav>`: 48 kHz mono 16-bit files in place of the microphone, and a recording of what is played.
- `TC2_NET_DEBUG=1`, `TC2_NET_SIM=loss%,stall_ms`: the library's own logging and simulated bad network.
- `TCVR_TEST_MENU=3@60`: open the ONLINE page at desktop frame 60.
- `TCVR_CAPTURE_EVERY=N`: save `cap-<frame>.ppm` every N frames.

## Limitations

- **Hardware so far (Quest 3, home Wi-Fi, nobody wearing the headset):**
  - The Quest hosted and a Windows desktop instance joined. The link was alive throughout, with 0 game errors on the Quest, 16-35 ms ping and 1-2 frames of lag, and the room reopened when the PC left.
  - Voice from the PC reached the Quest with no frame lost, and the Quest played it back. Its microphone opened in voice-communication mode, but the Quest 3 reports no separate echo-canceller effect.
  - Not tested yet: Quest-to-Quest, PCVR, actually playing linked in a headset, and voice quality judged by ear.
- **Through `zonesync.net` itself:** two desktop instances on this PC hosted and joined a room by code over the internet relay. About 50 ms ping, the link alive with 0 game errors, voice intact.
- **Rooms are listed publicly** in the browser; the code is a convenience, not a password.
- **15-minute server sessions:** servers end a session after 15 minutes. The lobby re-links within about a second and the arcade link is kept up meanwhile, but this path has not been exercised in a full-length game yet.
- **Same build:** both players need the same build, because the link version is checked by the lobby.
