# Online 2-player

Time Crisis II was built as a **twin cabinet**: two complete arcade boards, one per player, joined by a cable. Each board runs its own copy of the game and draws its own player's view; the cable keeps the two games in step. Keith (P1, left/red) and Robert (P2, right/blue) fight through the same stages from their own positions, see each other in the scene, and keep separate scores.

This build emulates that cable, the System 23 **C422** link chip, and carries it over the internet or your home network, with **voice chat** alongside. Both players run the full game, so there is no "host advantage", no rollback and no desync: it is the arcade's own two-cabinet mode, as Namco built it.

Quest, Windows PCVR and the Windows desktop mode all play with each other. **They also link with the desktop Time Crisis 2 of [namco22-decompile](https://github.com/spacestate1/namco22-decompile)**, the project this port is built on, 0.9.3 and later: the same lobby server, rooms and wire format. Tested against its 0.10.1 release. Voice chat works between VR builds; a desktop partner simply has none.

## Quick start

1. Both players install **the same build** of Time Crisis II VR Online.
2. Press the **left menu button** to open options, then **left stick up / down** to reach page **4/4 ONLINE 2-PLAYER**.
3. One player chooses **HOST ONLINE ROOM (CODE)**. The page shows a four-character room code, for example `KXQ7`.
4. **The host can play straight away.** Close the menu, insert credits (**A**) and shoot **SOLO/LINKED** ("SOLO START, BUT PERMITS 2P JOIN-IN"). Your friend can join that game whenever they arrive, as at a linked arcade. **SOLO ONLY** is there too: it locks out a second player for that game.
5. The other player chooses **JOIN ROOM CODE** and enters it, at any time. Alternatively, **BROWSE GAMES** lists every open room on the server and every game hosted on your local network; select one to join.
6. The games link automatically: the host becomes **P1 Keith**, the joiner **P2 Robert**. A banner confirms it: `LINKED WITH <NAME> - P2 ROBERT`. Voice chat starts at the same moment. On Quest, the first time, allow microphone access.
7. **P2's arcade restarts once** (`JOINING P1 - THE ARCADE RESTARTS, THEN COIN IN`): the warning screen, then attract, about 10 seconds. The arcade only takes in a second cabinet that shook hands on the link when it powered on, and the restart does that handshake. A P2 still in a game of its own finishes it first.
8. **P2 joins P1's game.** P2 inserts credits and pulls the trigger. If P1's game started less than 25 seconds ago, P2's coin, trigger and pedal are held until it has run that long. Banners count down (`P1 IS STARTING - YOU JOIN IN 12 S`), and coins put in meanwhile are paid in automatically.

   The 25 seconds are upstream's measured rule: if P2 is credited before or right at P1's start, the arcade can treat P1 as a computer partner and P2's kills stop counting. Both sides apply the same rule.

Either player can leave at any time. The other's game carries on, and the room stays open for the next partner. When P1's game ends, P1 can start a new one with SOLO/LINKED again, whether or not anyone is linked.

### Or start from the arcade's own screen

You don't have to open the menu first. Put coins in without a room open and you reach **SELECT GAME MODE** with **SOLO/LINKED** crossed out; the arcade ignores a shot at it. Here, **shooting it hosts a room for you**:

1. A room opens with a code (`ROOM KXQ7 OPEN - THE ARCADE RESTARTS, COINS KEPT`), and **your arcade restarts once**: the warning screen, then attract, about 12 seconds. The game decides whether SOLO/LINKED is open when the screen comes up, so it has to come up again.
2. Your coins are paid back in by themselves, and SELECT GAME MODE comes up again, now with SOLO/LINKED open (`ROOM KXQ7: SHOOT SOLO/LINKED - FRIENDS JOIN ANY TIME`).
3. Shoot **SOLO/LINKED** and play. Friends join with the code, or from **BROWSE GAMES**, whenever they arrive, as in the quick start. The code is also on the ONLINE page.

If the server cannot be reached, nothing restarts and the banner says so. Host before you put coins in and there is no restart at all. If you are joining someone rather than hosting, the shot opens the ONLINE page instead, with the arcade paused.

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
- **If your partner drops** (Wi-Fi gap, headset asleep, app restarted), the game behaves like a twin cabinet whose other half was switched off. P1's game goes on alone. The host reopens the room by itself, and a joiner keeps reconnecting to the same room or address for a minute. When the partner returns, the banner shows `<NAME> IS BACK - LINKED AGAIN`.
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

The game uses about **62 KB/s each way** (0.5 Mbit/s): one 1 KB packet per arcade frame, as upstream. Voice rides inside those packets and adds no traffic.

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
| P2's trigger and coin do nothing | P2 waits for P1's game to be under way (about 25 s); the banner counts down, and coins are paid in afterwards. |
| P2's SOLO/LINKED stays crossed out | P1 is playing **SOLO ONLY** (P2's banner says `P1 PLAYS SOLO ONLY - JOIN AFTER THEIR GAME`), or P1 put coins in before opening the room. P2 joins P1's next game. |
| `JOINING P1 - THE ARCADE RESTARTS` | P2's one restart for the link handshake. Put coins in again once attract is back. |
| `FINISH YOUR GAME - THEN YOU JOIN P1` | P2 is in a game of its own; the restart waits until it ends. |
| P1's SOLO/LINKED crossed out while hosting | The upstream desktop game, or `TC2_NO_STANDIN`, has no stand-in: link first, then put coins in. |
| `PARTNER'S GAME HAS NO VOICE CHAT` | Your partner runs the upstream desktop game; voice chat needs this VR build on both sides. |

Logs: the Quest log (`Export-Quest-Diagnostics.ps1`) and Windows `tc2.log` contain `[ONLINE]`, `[NET]`, `[LINK]` and `[VOICE]` lines.

## online.cfg

Beside the settings file (Quest: the app's files folder; Windows: next to the executable). It is created on first start.

```
name=PLAYER             # shown to your partner
server=zonesync.net     # room codes, the browser and QUICK MATCH: host or host:port
join=192.168.1.20:27750 # the last JOIN IP address
voice=1                 # voice chat on
mic_muted=0
voice_volume=100        # the partner's voice, 0-200
```

## How it works

### The link chip

The System 23 board's C422 is an RS-422 serial controller with 16 KB of buffer RAM (`0x06200000`) and eight 16-bit registers (`0x06400000`); its interrupt is CPU IRQ3 (Cause IP5). MAME only stubs it, so the cable was dead; upstream added its own model in 0.9.0, and this port keeps its own (below), which speaks the same wire format. The game's own driver, read from the translated program, defines what the chip must do:

| Routine | Role |
| --- | --- |
| `8000BEAC` | C422 interrupt: status bit 1 is receive, bit 2 is transmit; its caller acknowledges with a write to `0x0D000008` |
| `8000BB6C` | Transmit: sends up to 255 words per chunk (register 3 = 3 / 2 around it); register 7 is set once per packet and advanced by the chip |
| `8000BD24` | Receive: scans back from register 6 (the last word written in the 4096-word ring at words `0x1000-0x1FFF`) for the bit-8 trailer `[length hi][length lo]`, then checks the `0x5A` sync byte |
| `8000BF38` | Once a frame from the main loop: validates queued packets (byte sum 0) and builds the next one, sent three times |

Its variables (gp `0x802C7820`) give the link state shown on the ONLINE page: `0x802F3502` alive, `0x802F3504` frames behind the partner (lost above 16), `gp+0x75BC` state (0 linked, 2 lost) and three error counters.

`quest/tc2_link.c` implements the chip. Register 1 is an interrupt mask that the chip sets as it interrupts and the handler clears to re-arm. Register 6 points at the last word received. Received packets enter the ring one per chip tick (about 350 a frame); after a stall only the newest half-ring's worth (2048 words) is kept, because a burst of backlog would wrap the ring over packets the game has queued but not read. **While no session has started the chip is exactly the plain register stub** that MAME and upstream's unarmed model are, so single-player play is unchanged by it.

The pinned core is not modified. Since upstream 0.9.0 the core calls out for its link (`tc2_c422_read` / `tc2_c422_write` from `tc2_chips.c`; `tc2_link_id`, `tc2_link_hold_start` and `tc2_link_coin_inject` from the I/O board in `tc2_h8.c`), so the build compiles `quest/tc2_link.c`, `quest/tc2_cabinet.c` and `quest/tc2_online.c` in place of upstream's `tc2_link.c`. For the rest, `quest/tc2_sources.cmake` generates copies of the upstream files it hooks, and the configure step fails if any hook no longer matches upstream:

- `tc2_ctl.c` and `tc2_main.c`: the C422 handler's acknowledge, and its clock in instructions.
- `tc2_audio.c`: the voice mix.
- `tc2/src/eng/net.c` (Time Crisis 2's own copy of the NMN2 client): a room request for the next connect, and an accessor for the current room.

### Cabinet side

The lobby slot decides the cabinet: slot 0 is P1, slot 1 is P2. The game reads the TSS I/O cabinet-ID switch continuously, so the side is set when the session starts with no reboot. The single-player Robert-route adapter is not involved. The side is kept until the player leaves, so a dropped and resumed connection never moves a cabinet mid-game.

### Transport: compatible with upstream

The lobby and relay are upstream's NMN2 protocol (`upstream/docs/NETPLAY.md`): the client `upstream/tc2/src/eng/net.c` and the built-in host `upstream/tc2/src/eng/net_host.c`, the copies upstream's own Time Crisis 2 builds. On top of that, `quest/tc2_link.c` and `quest/tc2_online.c` follow upstream's own Time Crisis 2 online play (its `tc2/src/tc2_link.c`, 0.9.3 to 0.10.1, MIT), so VR and desktop cabinets link with each other:

- **Game, version and frame size:** game id `t2`, link version 1, 2 players, 1 KB frames. `zonesync.net` and current `nmn-server` builds list `t2` with 1 KB frames; an older server without that row caps unlisted games at 256 bytes and cannot carry it.
- **One payload per arcade frame:**

| Bytes | Content |
| --- | --- |
| 0 | k, the number of link frames |
| then k times | big-endian 16-bit id, big-endian 16-bit length n, n bytes (one byte per word; bit 8 is set only on the last word, so it is rebuilt on receipt) |
| after the link frames | **the voice block** (this build only): `V` `C`, a 16-bit length, then the Opus frames |
| 1016-1018 | the trailer: `T` `2` and the byte `0x802F3502` the sender sees (its partner's state: 1 attract, 2 credited, 4 in a game), which tells the partner its own state |
| 1019 | from this build (upstream leaves it 0 and never reads it): `J` our game takes a second player and has run 25 s (P2's agreed start ends at once), `L` our game is SOLO ONLY, `V` otherwise |
| 1020-1023 | the sender's frame number, big-endian |

- **Redundancy and duplicates:** the payload carries the newest transmitted link frames that fit, so every datagram repeats recent ones and a lost datagram costs nothing. The driver's resends of the same packet are carried once, and the receiver gives the game each id once.
- **Voice placement:** voice takes room from the repeated older frames, never from the newest. Upstream's parser stops after the k link frames, so it never sees the voice block.
- **Agreed start:** P2's coin, trigger and pedal are held, where the I/O board reads them (`tc2_h8.c` asks `tc2_link_hold_start`), until P2 is in a game or P1 has been in its game for `TC2_JOIN_WAIT` frames (1500, or the same time on the wall clock). Held coins are paid in afterwards, 4 frames on and 36 off each.
- **Stall-and-wait:** neither cabinet runs more than a few frames ahead of the other (2 plus the ping in frames, or `TC2_NET_LEAD`). Upstream blocks inside the network call; here the VR host asks before each arcade frame and keeps rendering the headset while it waits.

Our C422 model differs inside (its interrupt bookkeeping), but the words a cabinet puts on the cable are the game's own, so the two models interoperate. The cross-play test below checks this from both sides.

**Rooms.** A hosted room is created with the name `<NAME> <CODE>`. The code uses 26 characters that cannot be misread: no 0/O, 1/I, 2/Z, 5/S or 8/B. **JOIN ROOM CODE** and **BROWSE GAMES** ask the server for this game's rooms without joining any (`quest/tc2_rooms.c`): DISCOVER returns ANNOUNCE, and a ROOMS request padded to 400 bytes returns ROOMLIST, exactly as the server answers any stranger. They then join that room by id. Upstream players' rooms appear in the same list, without a code, and are joined from the browser. Once inside, every later reconnect asks for the same room.

`quest/tc2_online.c` runs the session. Joining counts as Ready, and the lower slot starts as soon as both are in. Once linked, the episode keeps the link enabled through session gaps and reconnects as described above. `quest/quest_host.c` keeps the arcade running behind menus and focus loss while linked, and polls the lobby every display frame.

### Drop in at any time: the stand-in partner

Measured on US TSS3 Ver. B (`quest/tc2_link.c` stand-in, `quest/tc2_online.c` `standin_tick` and `link_contact_tick`):

- **SELECT GAME MODE** comes up with the first credit. It waits with `0x2CE92B` = 1 and `0x2F3930` = 1 and times out to SOLO ONLY after about 15 s. Leaving it, `0x2F3930` goes 1 to 2 for SOLO ONLY and 1 to 4 for SOLO/LINKED.
- **SOLO/LINKED is open only if a partner is linked as the screen comes up** (`0x802F3502`, the partner's state: 0 none, 1 attract, 2 credited, 4 in a game, 5 handshake). A partner appearing later does not open it: the screen re-armed, shots on the panel did nothing, and it ran out into SOLO ONLY. Without a partner the game ignores a shot at the panel (x 20-632, y 95-258 of the 640x480 picture); the port hosts a room instead and restarts P1 (`mode_screen_tick`).
- **The coins** (0 to 9 coins against RAM): `0x2CE7B7` holds the coins in the machine and `0x2F3D27` counts every coin. A fresh cabinet takes 4 coins a credit. SELECT GAME MODE comes up as the first credit completes and takes it then (3 coins leave 3; 4 leave 0; 5 leave 1; 8 leave 4). A TEST restart clears them, so the drop-in restart pays back the credit plus the coins left, through the same coin input P2's held coins use (`repay_tick`). The cost of a credit is read off the counters whenever the game takes one. After the restart, SELECT GAME MODE waits with `0x2F3930` = 0 rather than 1.
- **The game's mode** is the word at `0x802F3FD0`: 1 SOLO ONLY, 2 SOLO/LINKED, 0 between games. About 30 places in the game branch on it.
- **A game in mode 2 takes a second player whenever one arrives.** Its partner may vanish mid-game, and a right cabinet that links later still takes LINK PLAY and plays alongside: score, lives, P1 seen from Robert's side. A right cabinet that was running before the link came up does not: it sees the left's game but its SOLO/LINKED stays crossed out. The game shakes hands on the link about 920 frames after power-on, and a right cabinet only joins after doing so with the link up. A left cabinet does not need it: hosting 50 s after power-on, SOLO/LINKED opened.
- **So while you host with nobody linked, a stand-in answers as an idle right cabinet.** Before a game, a right cabinet's packets are the left's own: the same frame counter (bytes 1-2) and header, cabinet byte 13 = 5 instead of 4, a few attract fields. Bytes 1 to n-3 sum to 0 (n-3 is the checksum); the last two give the length. The stand-in answers each packet the left sends: during the handshake with the packet itself, in attract as the right cabinet, and after that with that attract packet, its counter and header bytes 4-7 following the left's. A stand-in that starts while the left is credited uses an attract packet recorded from the game. No second game runs; the cost is one packet copy a frame. In an episode (a partner linked before), the stand-in comes back only between games, because mid-game the arcade still expects that partner.
- **A real partner replaces the stand-in** at once. If P1 is choosing on SELECT GAME MODE right then, the handover waits for the choice, since a link change would cross SOLO/LINKED out under the player. P2 then restarts once if it linked after its power-on handshake: the TEST switch pressed for 10 frames makes the game restart its own program (the Parental Advisory, then attract about 600 frames later, credits cleared). It waits until P2 is between games. The emulator is not reset; the game does it.
- **The difficulty** of SOLO/LINKED played alone is about that of SOLO ONLY. Over four seeds of a player standing exposed and never firing, SOLO ONLY took 10 hits and SOLO/LINKED 7, the first hit always a little later. Same stage, route, cutscenes and computer partner.

### Voice

`quest/tc2_voice.c`:

- **Capture:** 48 kHz mono, in 20 ms frames.
  - Desktop records through an SDL capture device.
  - Quest records in `MainActivity` with `AudioRecord` in `VOICE_COMMUNICATION` mode, plus the platform echo canceller and noise suppressor where the headset provides them.
- **Send:** a level gate, then Opus VOIP at 20 kbit/s with in-band error correction, carried in the voice block of the link payload.
- **Receive:**
  - A 64-slot jitter store, decoded about 60 ms ahead.
  - A missing frame is rebuilt from the next frame's error correction, or by Opus concealment.
  - The result is resampled to the game's 88.2 kHz and added to both channels of its output, because Android allows one output device.

Opus 1.5.2 is a pinned download (`tools/bootstrap.py`) built into both apps.

## Testing

```powershell
python -X utf8 tools/build_pc.py --rom timecrs2.zip --jobs 8
# upstream's CURRENT lobby server (it knows Time Crisis 2's 1 KB frames), from the submodule's remote
git -C upstream fetch origin; mkdir build/nmn-server-head; git -C upstream archive origin/HEAD server | tar -x -C build/nmn-server-head
cd build/nmn-server-head/server; $env:CARGO_TARGET_DIR="../target"; cargo build --release; cd ../../..
# optional, for the cross-play scenario: upstream's own Windows release, extracted, with timecrs2.zip in its roms folder
gh release download 0.10.1 -R spacestate1/namco22-decompile -p "windows-release-0.10.1.zip" -D build/upstream-release
python -X utf8 tests/test_online.py            # 18 scenarios + crossplay, about 35 minutes in real time
python -X utf8 tests/test_handedness.py        # includes the ONLINE page's input routing
```

`tests/test_online.py` starts two real desktop instances, both booted as P1:

- **Running:** A hosts and B joins, over loopback or through a local `nmn-server` bound to 127.0.0.1. Both play the same scripted coins, start and shooting.
- **Microphones:** replaced by WAV files, a 440 Hz tone for A and silence for B. What each side plays back is recorded and checked for that tone. Every cabinet runs on SDL's silent audio device, so nothing plays aloud.
- **Firewall:** the cabinets always run from `build/online-test/cabinets/A` and `B`, so Windows asks once per path. From a git worktree, `TC2_TEST_BUILD=<main checkout>/build` reuses that checkout's folders, server and upstream release.
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
| `p2join` | Linked first; A starts LINK PLAY, B puts coins in at 3000 (held, then paid in) and takes LINK PLAY on the right cabinet |
| `modescreen` | A, credited with no room, shoots the crossed-out SOLO/LINKED: that shot hosts a room and A restarts; its 8 coins are paid back, SELECT GAME MODE comes up by itself and A takes SOLO/LINKED alone; B joins a while into A's game, restarts once for its handshake, and joins it |
| `dropin` | A hosts and takes SOLO/LINKED alone at once (the stand-in); B joins a minute into A's game, restarts once, and takes LINK PLAY with no agreed-start wait |
| `crossplay` | Our cabinet (P1, hosting a room) against **upstream's released desktop game** (P2, headless, joining by quick match). It checks the link from both sides, 0 link errors, upstream's agreed start holding and releasing on our trailer, and that a desktop partner is not shown voice chat. Skipped without the download |

Test switches, read by `quest/tc2_online.c` and `quest/tc2_voice.c`:

- `TC2_NET_SCRIPT="600:hostroom;2400:mute"`: actions run at those arcade frames, or `t<ms>:` milliseconds after start.
  - Actions: `host`, `join=<addr>`, `discover`, `quick`, `hostroom`, `joincode=<code>`, `browse`, `pickroom`, `mute`, `voice=0|1`, `leave`, `freeze=<ms>`.
  - On a headset, `online-test.txt` beside the settings (written with `adb run-as` on this debuggable build) holds `script=...`, `status=1` and `voice_out=<file>`.
- `TC2_NET_STATUS=1`: a status line every 300 frames.
- `TC2_NET_ROOM_CODE=KXQ7`: a fixed code for hosted rooms.
- `TC2_VOICE_IN=<wav>`, `TC2_VOICE_OUT=<wav>`: 48 kHz mono 16-bit files in place of the microphone, and a recording of what is played.
- `TC2_NET_DEBUG=1`, `TC2_NET_SIM=loss%,stall_ms`: the library's own logging and simulated bad network.
- `TCVR_TEST_MENU=3@60`: open the ONLINE page at desktop frame 60.
- `TCVR_RAMWATCH=2CE92B,2F3930`: log each main-RAM byte (hex offsets) whenever it changes.
- `TC2_NO_MODE_MENU=1`: a shot at the crossed-out SOLO/LINKED does nothing, as at the arcade (`late` links into a game on purpose).
- `TC2_NO_STANDIN=1`: no stand-in partner while hosting (SOLO/LINKED then needs a linked partner, as before).
- `TCVR_LINKDUMP=<file>`: every link packet this cabinet sends, as hex with its frame (how the stand-in was worked out).
- `TCVR_RAMDUMP=<frame>[,<frame>...]`: main RAM to `ram-<frame>.bin` at those frames (how the coin counters were found, by comparing runs with different coins). `TCVR_RAMWATCH=<addr>[,...]` logs chosen bytes as they change.
- `TCVR_CAPTURE_EVERY=N`: save `cap-<frame>.ppm` every N frames.

## Limitations

- **Hardware so far (Quest 3, home Wi-Fi, nobody wearing the headset):**
  - The Quest hosted and a Windows desktop instance joined. The link was alive throughout, with 0 game errors on the Quest, 16-35 ms ping and 1-2 frames of lag, and the room reopened when the PC left.
  - Voice from the PC reached the Quest with no frame lost, and the Quest played it back. Its microphone opened in voice-communication mode, but the Quest 3 reports no separate echo-canceller effect.
  - Not tested yet: Quest-to-Quest, PCVR, actually playing linked in a headset, and voice quality judged by ear.
- **Through `zonesync.net` itself:** two desktop instances on this PC hosted and joined a room by code over the internet relay. About 50 ms ping, the link alive with 0 game errors, voice intact.
- **Rooms are listed publicly** in the browser; the code is a convenience, not a password.
- **15-minute server sessions:** servers end a session after 15 minutes. The lobby re-links within about a second and the arcade link is kept up meanwhile, but this path has not been exercised in a full-length game yet.
- **Same wire format:** players need this build or an upstream namco22-decompile 0.9.3+ desktop game; the lobby checks game id, version and frame size.
- **The agreed start:** our own tests also saw P2's kills count when P2 started first, but the hold is upstream's measured rule and both builds apply it.
