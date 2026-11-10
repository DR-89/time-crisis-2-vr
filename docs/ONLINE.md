# Online 2-player

Time Crisis II was built as a **twin cabinet**: two complete arcade boards, one per player, joined by a cable. Each board runs its own copy of the game and draws its own player's view; the cable keeps the two games in step. Keith (P1, left/red) and Robert (P2, right/blue) fight through the same stages from their own positions, see each other in the scene, and keep separate scores.

This build emulates that cable, the System 23 **C422** link chip, and carries it over the internet or your home network, with **voice chat** alongside. Both players run the full game, so there is no "host advantage", no rollback and no desync: it is the arcade's own two-cabinet mode, as Namco built it.

Quest, Windows PCVR and the Windows desktop mode all play with each other. **They also link with the desktop Time Crisis 2 of [namco22-decompile](https://github.com/spacestate1/namco22-decompile)**, the project this port is built on, 0.9.3 and later: the same lobby server, rooms and wire format. Tested against its 0.10.1 release. Voice chat works between VR builds; a desktop partner simply has none.

## Quick start

1. Both players install **the same build** of Time Crisis II VR Online.
2. Press the **left menu button** to open the menu and pick the **ONLINE** tab (point the laser and pull the trigger, or squeeze the right grip to step through the tabs).
3. One player chooses **HOST A ROOM**. The tab shows a four-character room code in large letters, for example `KXQ7`.
4. **The host can play straight away.** Close the menu, insert credits (**A**) and shoot **SOLO/LINKED** ("SOLO START, BUT PERMITS 2P JOIN-IN"). Your friend can join that game whenever they arrive, as at a linked arcade. **SOLO ONLY** is there too: it locks out a second player for that game.
5. The other player chooses **JOIN WITH CODE** and types it on the on-screen keyboard, at any time. Alternatively, **BROWSE GAMES** lists every open room on the server and every game hosted on your local network; select one to join.
6. The games link automatically: the host becomes **P1 Keith**, the joiner **P2 Robert**. A banner confirms it: `LINKED WITH <NAME> - P2 ROBERT`. Voice chat starts at the same moment. On Quest, the first time, allow microphone access.
7. **P2's arcade restarts once** (`JOINING P1 - THE ARCADE RESTARTS, THEN COIN IN`): the warning screen, then attract, about 10 seconds. The arcade only takes in a second cabinet that shook hands on the link when it powered on, and the restart does that handshake. A P2 still in a game of its own finishes it first.
8. **P2 joins P1's game.** Once P2's restart is done and P1's game is running, **one credit goes in for P2 by itself** (`A CREDIT TO JOIN WITH - PULL THE TRIGGER`): P2 pulls the trigger and takes LINK PLAY. That is **Join with a credit** on the ONLINE tab, on by default. It happens once per session; continues still take P2's own coins (**A**), so a one-credit run stays a one-credit run. Turned off, P2 inserts credits as at the arcade; until then P2's screen shows P1's game. If P1's game started less than 25 seconds ago, P2's coin, trigger and pedal are held until it has run that long. Banners count down (`P1 IS STARTING - YOU JOIN IN 12 S`), and coins put in meanwhile are paid in automatically.

   The 25 seconds are upstream's measured rule: if P2 is credited before or right at P1's start, the arcade can treat P1 as a computer partner and P2's kills stop counting. Both sides apply the same rule.

Either player can leave at any time. The other's game carries on, and the room stays open for the next partner. When P1's game ends, P1 can start a new one with SOLO/LINKED again, whether or not anyone is linked.

### Or start from the arcade's own screen

You don't have to open the menu first. **SOLO/LINKED is always open** on SELECT GAME MODE, from power-on, because a stand-in answers on the link as an idle second cabinet (see [Drop in at any time](#drop-in-at-any-time-the-stand-in-partner)). Put coins in and shoot **SOLO/LINKED**; the game starts at once, as SOLO/LINKED does at a linked arcade, and **a room opens for it by itself** (`ROOM KXQ7 - FRIENDS JOIN ANY TIME`). Friends join with the code, or from **BROWSE GAMES**, whenever they arrive. The code is also on the ONLINE tab.

**SOLO ONLY** opens no room and locks out a second player for that game. **The menu never chooses for you.** If nobody shoots before its 15 seconds run out, the arcade goes back to its intro (the warning screen, then attract) with your coins kept (`COINS KEPT - PULL THE TRIGGER TO PLAY`). Pull the trigger at any time and SELECT GAME MODE is back within a second, with its full 15 seconds. If the server cannot be reached, the game goes on alone and the banner says so.

Room codes go through the lobby server, so they work across the internet with no router setup, VPN or addresses. Other ways to meet:

| Item | What it does |
| --- | --- |
| **HOST A ROOM** | Opens a room on the lobby server and shows its code |
| **JOIN WITH CODE** | Joins the room with that code (an on-screen keyboard) |
| **BROWSE GAMES** | Lists the server's rooms (name, players, `JOIN` / `PLAYING` / `FULL` / `OLD` version) and local-network games, five to a page; refreshes every 4 seconds |
| **LOCAL NETWORK** | **HOST ON THIS NETWORK** (the built-in host on UDP 27750; it lists your addresses, VPN ones marked), the games found on your network, and **JOIN BY IP** with a number pad, on the LAN or over the internet |
| **QUICK MATCH** | Joins whoever is waiting on the server |
| **REJOIN** | After you leave a room you were linked in (or restart out of it), takes you back while the room is still open |

The menu works the same on every tab (see the [README](../README.md#the-menu)): point the gun's laser and pull the trigger, or move with either stick, **A** to select and **B** to go back; the desktop uses the mouse, the arrows, Enter and Backspace. Codes, names and addresses are typed on an on-screen keyboard. **CHANGE** next to your name sets the name your partner sees (letters, digits, `-`, `_`; 12 characters).

## Voice chat

- **On by default.** It is live only while you are linked to a partner. **Voice chat** on the ONLINE tab chooses **OPEN MIC**, **PUSH TO TALK** or **OFF**.
- **OPEN MIC** (the default) sends when you speak: speech above about -42 dBFS opens it, and it stays open 0.4 s after you stop. Silence, gunfire and fans cost nothing.
  - **Mute** with a **left stick click** during play, **M** on the desktop, or **MUTE** on the ONLINE tab. A banner confirms `MICROPHONE MUTED` / `MICROPHONE ON`.
- **PUSH TO TALK** sends only while you hold **either thumbstick click** (**V** on a PC), and 0.2 s after you let go. The ONLINE tab shows `TALKING` while it sends.
- **Partner volume** is 0-200%, set with **Partner volume**. Your partner's voice is mixed into the game's sound.
- While linked, the ONLINE tab shows a level meter for you and for your partner.
- **Echo cancellation and noise suppression**, on every headset and PC, before anything is sent:
  - The echo canceller ([SpeexDSP](https://github.com/xiph/speexdsp)) knows what your speakers play (the game and your partner) and takes it out of your microphone. It learns your room in the first few seconds of a session. Measured in the tests: about 20 dB of the echo off once it has learned, with the speaker 40-200 ms late. Before it, a speaker turned up loud sent the partner's own voice back all session; now only the first ~3 seconds.
  - Noise suppression ([RNNoise](https://github.com/xiph/rnnoise)) takes out steady noise (fans, hum, hiss) and tells speech apart from everything else, so the gate opens for your voice and not for your game's gunfire.
  - The gate still asks for more while your partner is speaking: twice the level once the echo canceller has learned the room, four times in a session's first 10 seconds. Talking over each other still works (tested).
  - Quest also records through Android's voice-communication input, which turns its speakers down in the microphone. The Quest 3 offers no separate echo-canceller effect of its own (measured).
- On Quest, if microphone access is refused, the banner says `NO MICROPHONE ACCESS - LISTEN ONLY`. You still hear your partner. Allow it later in the headset's app permissions.
- Bandwidth is about 3 KB/s while talking: Opus at 20 kbit/s with in-band error correction.

## While you are linked

- **The game keeps running while the menu is open, while the headset is off and while the app is in the background.** Your partner's cabinet needs yours, exactly as at the arcade. Your controls are released while you are away. On Quest this holds until the headset goes to sleep; after that the reconnection below takes over.
- **If your partner drops** (Wi-Fi gap, headset asleep, app restarted), the game behaves like a twin cabinet whose other half was switched off. P1's game goes on alone. The host reopens the room by itself, and a joiner keeps reconnecting to the same room or address for a minute. When the partner returns, the banner shows `<NAME> IS BACK - LINKED AGAIN`.
- **Leaving:** ONLINE tab, **LEAVE ONLINE GAME**, or **RESTART & LEAVE** at the bottom of the menu. You go back to single-player on your usual cabinet side, and **REJOIN** waits on the ONLINE tab while the room stays open.
- **If the connection can't be restored within a minute,** you go back to single-player and **REJOIN** waits for you too.

## Coming back: REJOIN

**REJOIN** on the ONLINE tab takes you back into the room you left, restarted out of or lost the connection to. It is there as long as the server still lists that room (while your partner is in it), and it survives closing the app: after a restart it appears once the server has confirmed the room is still open, for up to 12 hours.

You come back as the same cabinet:

- **As P2,** your arcade restarts once for its handshake and you drop into P1's game, as when you first joined.
- **As P1,** if your partner is in the middle of a game, you start a new game of your own: a left cabinet that was switched off can't drop back into its partner's game, at the arcade either. Your partner joins your game after theirs ends (the banner says `BACK AS P1 - START A GAME, <NAME> JOINS AFTER THEIRS`). Between games, you simply play together again.
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
| `NO ANSWER - CHECK ADDRESS / PORT 27750` | Check the host's address on their ONLINE tab. Over the internet, the host must forward **UDP** 27750, or both of you must be on the same Tailscale/ZeroTier network. Room codes avoid this. |
| A Windows host is never found | When Windows asks whether `TimeCrisis2VR.exe` may use the network, allow it on **Private networks**. Otherwise add an inbound rule for UDP 27750. |
| A LAN game is not listed | Some routers block broadcasts between Wi-Fi clients ("AP/client isolation") or between 2.4 and 5 GHz bands. Use a room code, or **JOIN IP** with the address shown on the host's page. |
| `CANNOT HOST: PORT 27750 IS IN USE` | Another copy of the game (or an `nmn-server`) is already hosting on this machine. |
| `PARTNER CONNECTION IS LAGGING` | Prefer 5 GHz Wi-Fi or Ethernet for the PC, and close large downloads. The arcade link gives up after 16 silent frames and recovers on its own. |
| Your partner hears an echo of themselves | It should stop within a few seconds of linking, once the echo canceller has learned your room. If not, turn your speakers down a little, use **PUSH TO TALK**, or wear headphones. |
| No voice | Check **VOICE CHAT: ON**, the **MICROPHONE** state, and on Quest the app's microphone permission. On Windows, check that desktop apps may use the microphone (Privacy settings). |
| Linked with a stranger | **QUICK MATCH** pairs whoever is waiting; for a friend, use a room code. |
| P2's trigger and coin do nothing | P2 waits for P1's game to be under way (about 25 s); the banner counts down, and coins are paid in afterwards. |
| P2's SOLO/LINKED stays crossed out | P1 is playing **SOLO ONLY** (P2's banner says `P1 PLAYS SOLO ONLY - JOIN AFTER THEIR GAME`). P2 joins P1's next game. |
| `JOINING P1 - THE ARCADE RESTARTS` | P2's one restart for the link handshake. Put coins in again once attract is back. |
| `FINISH YOUR GAME - THEN YOU JOIN P1` | P2 is in a game of its own; the restart waits until it ends. |
| P1's SOLO/LINKED crossed out while hosting | The upstream desktop game, or `TC2_NO_STANDIN`, has no stand-in: link first, then put coins in. |
| `PARTNER'S GAME HAS NO VOICE CHAT` | Your partner runs the upstream desktop game; voice chat needs this VR build on both sides. |

Logs: the Quest log (`Export-Quest-Diagnostics.ps1`) and Windows `tc2.log` contain `[ONLINE]`, `[NET]`, `[LINK]` and `[VOICE]` lines.
While linked, a `[LINKHEALTH]` line comes every 30 s, and at once when link frames go missing or the game counts link errors: `skipped` is the partner's link frames lost with every payload that carried them, `dropped` the received ones trimmed after a stall (a few at the start of a session are normal), `game-errors` the game's own link error counters, and `holds` / `held` / `longest` the pauses since the last line (the arcade waiting for its partner's frames: the hitches a player feels). Something going wrong in a linked game (an enemy only one cabinet sees, a boss that vanishes) should be reported with both players' logs: the two lines around that moment show whether the cabinets fell out of step. Measured with 5% loss and jitter (`badnet`, `voicenet`): 1-2 frames skipped in two minutes, 0 game errors; on a clean connection none.
  - The game's own link protocol sends each packet three times a frame and has its own resend count, as a cable link that may drop a word. Delivery made reliable on top (each side acknowledging, the sender resending what is unacknowledged, the receiver taking frames only in order) was tried and taken out (2026-11-10): some link frames are so large that one fills a payload, the unacknowledged backlog outgrew the history kept for resending, and a receiver waiting for a frame that could no longer come left the game 17 frames without link traffic, which it counts as the link lost.

## online.cfg

Beside the settings file (Quest: the app's files folder; Windows: next to the executable). It is created on first start.

```
name=PLAYER             # shown to your partner
server=zonesync.net     # room codes, the browser and QUICK MATCH: host or host:port
join=192.168.1.20:27750 # the last JOIN IP address
voice=1                 # voice chat on
mic_muted=0
voice_volume=100        # the partner's voice, 0-200
push_to_talk=0          # 1: send only while a thumbstick click (V) is held
join_credit=1           # a credit for you when you join a game as P2
rejoin=12 KXQ7 1 1794200000 SAM   # REJOIN: room id, code, your cabinet, when you left (Unix time), partner; written and removed by the game
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

Its variables (gp `0x802C7820`) give the link state shown on the ONLINE tab: `0x802F3502` alive, `0x802F3504` frames behind the partner (lost above 16), `gp+0x75BC` state (0 linked, 2 lost) and three error counters.

`quest/tc2_link.c` implements the chip. Register 1 is an interrupt mask that the chip sets as it interrupts and the handler clears to re-arm. Register 6 points at the last word received. Received packets enter the ring one per chip tick (about 350 a frame); after a stall only the newest half-ring's worth (2048 words) is kept, because a burst of backlog would wrap the ring over packets the game has queued but not read. **While no session has started the chip is exactly the plain register stub** that MAME and upstream's unarmed model are, so single-player play is unchanged by it.

The pinned core is not modified. Since upstream 0.9.0 the core calls out for its link (`tc2_c422_read` / `tc2_c422_write` from `tc2_chips.c`; `tc2_link_id`, `tc2_link_hold_start` and `tc2_link_coin_inject` from the I/O board in `tc2_h8.c`), so the build compiles `quest/tc2_link.c`, `quest/tc2_cabinet.c` and `quest/tc2_online.c` in place of upstream's `tc2_link.c`. For the rest, `quest/tc2_sources.cmake` generates copies of the upstream files it hooks, and the configure step fails if any hook no longer matches upstream:

- `tc2_ctl.c` and `tc2_main.c`: the C422 handler's acknowledge, and its clock in instructions.
- `tc2_audio.c`: the voice mix.
- `tc2/src/eng/net.c` (Time Crisis 2's own copy of the NMN2 client): a room request for the next connect, and an accessor for the current room.

### Cabinet side

The game reads the TSS I/O cabinet-ID switch continuously, so the side is set when the session starts with no reboot. The single-player Robert-route adapter is not involved.

The lobby slot alone can't decide it: the server gives whoever comes back the lowest free slot, and the player who stayed reconnects to reopen the room, so the slots swap (measured: a P1 who restarted out and rejoined came back as P2, and its partner was moved to P1 mid-game). So the two cabinets agree, through a side block in every frame (below). A cabinet that stayed keeps its side; one coming back through REJOIN asks for the side it had; a newcomer takes the other side; with no claim on either side, the slot decides (slot 0 P1), as before. Both ask for the same: the one that stayed keeps it, else the lower slot. The link starts once the sides are agreed, about one round trip after the session begins; a cabinet that stayed keeps its link running meanwhile. A partner without the block (upstream's desktop game, an older build of this one) is recognised by its first frame, and then the slot decides at once. The side is kept until the player leaves.

### Transport: compatible with upstream

The lobby and relay are upstream's NMN2 protocol (`upstream/docs/NETPLAY.md`): the client `upstream/tc2/src/eng/net.c` and the built-in host `upstream/tc2/src/eng/net_host.c`, the copies upstream's own Time Crisis 2 builds. On top of that, `quest/tc2_link.c` and `quest/tc2_online.c` follow upstream's own Time Crisis 2 online play (its `tc2/src/tc2_link.c`, 0.9.3 to 0.10.1, MIT), so VR and desktop cabinets link with each other:

- **Game, version and frame size:** game id `t2`, link version 1, 2 players, 1 KB frames. `zonesync.net` and current `nmn-server` builds list `t2` with 1 KB frames; an older server without that row caps unlisted games at 256 bytes and cannot carry it.
- **One payload per arcade frame:**

| Bytes | Content |
| --- | --- |
| 0 | k, the number of link frames |
| then k times | big-endian 16-bit id, big-endian 16-bit length n, n bytes (one byte per word; bit 8 is set only on the last word, so it is rebuilt on receipt) |
| after the link frames | **the voice block** (this build only): `V` `C`, a 16-bit length, then the Opus frames |
| 1012-1015 | from this build (upstream and older builds read nothing here): the side block, `S` `D`, a byte with the side asked for (bits 0-1: 0 none, 1, 2), whether the sender stayed (bit 2) and the side it has (bits 3-4, 0 while agreeing), then that byte xor `0x5A` |
| 1016-1018 | the trailer: `T` `2` and the byte `0x802F3502` the sender sees (its partner's state: 1 attract, 2 credited, 4 in a game), which tells the partner its own state |
| 1019 | from this build (upstream leaves it 0 and never reads it): `J` our game takes a second player and has run 25 s (P2's agreed start ends at once), `L` our game is SOLO ONLY, `V` otherwise |
| 1020-1023 | the sender's frame number, big-endian |

- **Redundancy and duplicates:** the payload carries the newest transmitted link frames that fit, so every datagram repeats recent ones and a lost datagram costs nothing. The driver's resends of the same packet are carried once, and the receiver gives the game each id once.
- **Voice placement:** voice takes room from the repeated older frames, never from the newest. Upstream's parser stops after the k link frames, so it never sees the voice block.
- **Agreed start:** P2's coin, trigger and pedal are held, where the I/O board reads them (`tc2_h8.c` asks `tc2_link_hold_start`), until P2 is in a game or P1 has been in its game for `TC2_JOIN_WAIT` frames (1500, or the same time on the wall clock). Held coins are paid in afterwards, 4 frames on and 36 off each.
  - Once P1 has played a game in the session, the hold applies only while P1 starts another (credited, or in a game younger than the wait). With P1 in attract after its game over, P2 plays on, continues and starts again by itself, as the arcade's right cabinet does when the left player walks away. Measured (`p2continue`): P2 continued twice after P1's game over, its score kept rising (its kills count, the partner shown as COM), and it started a new game afterwards. Before P1's first game P2 still waits, as upstream measured.
- **Stall-and-wait:** neither cabinet runs more than a few frames ahead of the other: upstream's 2 plus the ping in frames, and 6 more, up to 12 in all (a slower link keeps upstream's), or `TC2_NET_LEAD`.
  - Upstream's lead alone held the game at every Wi-Fi jitter spike. Measured with 5% loss and 150 ms jitter (`badnet`, a minute and a half): upstream's lead paused 26-28 times (about 2 s); 8 frames 7-9 times; 10, 1-6; 12, 2-4. The game's own link lag stayed at 7 frames or less, with no link loss and no game errors at any of them (the game declares the link lost near 17). Upstream blocks inside the network call; here the VR host asks before each arcade frame and keeps rendering the headset while it waits.

Our C422 model differs inside (its interrupt bookkeeping), but the words a cabinet puts on the cable are the game's own, so the two models interoperate. The cross-play test below checks this from both sides.

**Rooms.** A hosted room is created with the name `<NAME> <CODE>`. The code uses 26 characters that cannot be misread: no 0/O, 1/I, 2/Z, 5/S or 8/B. **JOIN ROOM CODE** and **BROWSE GAMES** ask the server for this game's rooms without joining any (`quest/tc2_rooms.c`): DISCOVER returns ANNOUNCE, and a ROOMS request padded to 400 bytes returns ROOMLIST, exactly as the server answers any stranger. They then join that room by id. Upstream players' rooms appear in the same list, without a code, and are joined from the browser. Once inside, every later reconnect asks for the same room.

`quest/tc2_online.c` runs the session. Joining counts as Ready, and the lower slot starts as soon as both are in. Once linked, the episode keeps the link enabled through session gaps and reconnects as described above. `quest/quest_host.c` keeps the arcade running behind menus and focus loss while linked, and polls the lobby every display frame.

### Drop in at any time: the stand-in partner

Measured on US TSS3 Ver. B (`quest/tc2_link.c` stand-in, `quest/tc2_online.c` `standin_tick` and `link_contact_tick`):

- **SELECT GAME MODE** comes up with the first credit. It waits with `0x2CE92B` = 1 and `0x2F3930` = 1 and counts its 900 frames down in the 16-bit word at `0x2F3933`; at 0 the game chooses by itself (SOLO ONLY, or SOLO/LINKED when that is open). The port restarts the arcade (TEST) 30 frames before that and keeps the coins for the trigger (`menu_timeout_tick`). After a TEST restart, coins count again from 30 frames in, during the warning screen, and SELECT GAME MODE comes up as soon as a credit completes; `0x2CE92B` stays 1 and the screen waits with `0x2F3930` = 0. Kept coins go in 6 frames on, 6 off. A pedal held as the game starts opens the game's GUN INITIALIZATION screen (30 s). Leaving the mode screen, `0x2F3930` goes 1 to 2 for SOLO ONLY and 1 to 4 for SOLO/LINKED.
- **SOLO/LINKED is open only if a partner is linked as the screen comes up** (`0x802F3502`, the partner's state: 0 none, 1 attract, 2 credited, 4 in a game, 5 handshake). A partner appearing later does not open it: the screen re-armed, shots on the panel did nothing, and it ran out into SOLO ONLY. Without a partner the game draws SOLO/LINKED crossed out and ignores a shot at the panel (x 20-632, y 95-258 of the 640x480 picture). With a partner (or the stand-in) it draws the panel plainly, and the screen's timeout picks SOLO/LINKED rather than SOLO ONLY.
- **The coins** (0 to 9 coins against RAM): `0x2CE7B7` holds the coins in the machine and `0x2F3D27` counts every coin. A fresh cabinet takes 4 coins a credit. SELECT GAME MODE comes up as the first credit completes and takes it then (3 coins leave 3; 4 leave 0; 5 leave 1; 8 leave 4). A TEST restart clears them, so the drop-in restart pays back the credit plus the coins left, through the same coin input P2's held coins use (`repay_tick`). The cost of a credit is read off the counters whenever the game takes one. After the restart, SELECT GAME MODE waits with `0x2F3930` = 0 rather than 1.
- **The game's mode** is the word at `0x802F3FD0`: 1 SOLO ONLY, 2 SOLO/LINKED, 0 between games. About 30 places in the game branch on it.
- **A game in mode 2 takes a second player whenever one arrives.** Its partner may vanish mid-game, and a right cabinet that links later still takes LINK PLAY and plays alongside: score, lives, P1 seen from Robert's side. A right cabinet that was running before the link came up does not: it sees the left's game but its SOLO/LINKED stays crossed out. The game shakes hands on the link about 920 frames after power-on, and a right cabinet only joins after doing so with the link up. A left cabinet does not need it: hosting 50 s after power-on, SOLO/LINKED opened.
- **So, from power-on, a stand-in answers as an idle right cabinet whenever nobody is linked** (a P1 cabinet; not a P2 one). Taking SOLO/LINKED with no room open hosts one (`standin_tick`). Before a game, a right cabinet's packets are the left's own: the same frame counter (bytes 1-2) and header, cabinet byte 13 = 5 instead of 4, a few attract fields. Bytes 1 to n-3 sum to 0 (n-3 is the checksum); the last two give the length. The stand-in answers each packet the left sends: during the handshake with the packet itself, in attract as the right cabinet, and after that with that attract packet, its counter and header bytes 4-7 following the left's. A stand-in that starts while the left is credited uses an attract packet recorded from the game. No second game runs; the cost is one packet copy a frame. Measured on the desktop build: about 10 µs a frame of link-chip work against about 3 ms for the whole arcade frame; the emulated CPU runs the same instructions either way. In an episode (a partner linked before), the stand-in comes back only between games, because mid-game the arcade still expects that partner.
- **A real partner replaces the stand-in** at once. If P1 is choosing on SELECT GAME MODE right then, the handover waits for the choice, since a link change would cross SOLO/LINKED out under the player. P2 then restarts once if it linked after its power-on handshake: the TEST switch pressed for 10 frames makes the game restart its own program (the Parental Advisory, then attract about 600 frames later, credits cleared). It waits until P2 is between games. The emulator is not reset; the game does it.
- **The difficulty** of SOLO/LINKED played alone is about that of SOLO ONLY. Over four seeds of a player standing exposed and never firing, SOLO ONLY took 10 hits and SOLO/LINKED 7, the first hit always a little later. Same stage, route, cutscenes and computer partner.

### Voice

`quest/tc2_voice.c`:

- **Capture:** 48 kHz mono, in 20 ms frames.
  - Desktop records through an SDL capture device.
  - Quest records in `MainActivity` with `AudioRecord` in `VOICE_COMMUNICATION` mode, plus the platform echo canceller and noise suppressor where the headset provides them.
- **Clean-up** (`quest/tc2_voice_dsp.c`), 10 ms blocks, on the thread that records:
  - Echo cancellation: SpeexDSP's MDF filter, 250 ms long, against what the game queues for the speakers (the game's sound with the partner's voice mixed in, converted to 48 kHz).
  - The two sides meet on one clock. Each block of the game's output is placed by when it will leave the audio queue (now plus what is queued ahead of it), each block of the microphone by when it arrived. Blocks then follow each other exactly, and the measured times only steer the running position slowly, so a device's 10 ms bursts do not shake the echo path the filter has learned. When the game is held (a stall, the link waiting for its partner) the queue runs dry and the device plays silence; the next block goes out at once and the position jumps there.
  - Noise suppression: RNNoise 0.2 with its trained model, which also gives each block a speech probability.
  - `tests/voice_dsp_fixture.c` runs it on a simulated clock: the queue, the device's buffers, a 60 ms stall every 7 s, the room and the recorder's jitter. About 20 dB of echo off (0.5 dB without the echo canceller), the player's voice kept at full level while the partner talks, game noise taken for speech under 1% of the time. About 0.15 ms per 10 ms block on a desktop core; the Quest's log reports its own figure (`[VOICE] microphone clean-up`).
- **Send:** a gate (open mic: speech, by RNNoise's probability, above a level; push to talk: the key), then Opus VOIP at 20 kbit/s with in-band error correction, carried in the voice block of the link payload.
- **Receive:**
  - A 64-slot jitter store, decoded about 60 ms ahead.
  - A missing frame is rebuilt from the next frame's error correction, or by Opus concealment.
  - The result is resampled to the game's 88.2 kHz and added to both channels of its output, because Android allows one output device.

Opus 1.5.2, RNNoise 0.2 and SpeexDSP 1.2.1 (BSD licences) are pinned downloads (`tools/bootstrap.py`) built into both apps.

## Testing

```powershell
python -X utf8 tools/build_pc.py --rom timecrs2.zip --jobs 8
# upstream's CURRENT lobby server (it knows Time Crisis 2's 1 KB frames), from the submodule's remote
git -C upstream fetch origin; mkdir build/nmn-server-head; git -C upstream archive origin/HEAD server | tar -x -C build/nmn-server-head
cd build/nmn-server-head/server; $env:CARGO_TARGET_DIR="../target"; cargo build --release; cd ../../..
# optional, for the cross-play scenario: upstream's own Windows release, extracted, with timecrs2.zip in its roms folder
gh release download 0.10.1 -R spacestate1/namco22-decompile -p "windows-release-0.10.1.zip" -D build/upstream-release
python -X utf8 tests/test_online.py            # 25 scenarios + crossplay on four lanes, about 12 minutes in real time
python -X utf8 tests/test_online.py --quick    # one of each kind (LAN, room and voice, drop-in, coming back, newcomer, crossplay), about 4 minutes
python -X utf8 tests/test_online.py dropin p1return   # just these; --serial runs one at a time, --keep keeps screenshots
python -X utf8 tests/test_handedness.py        # the host's input: menu laser, sticks, grips, A / B, the ONLINE tab's open / close
python -X utf8 tests/test_menu.py             # every menu tab and online state fits; its controls work
```

Every scenario runs two whole cabinets in real time (the link, the reconnection timers and the arcade's own timings depend on it), so the scenarios run side by side on lanes: the LAN ones one after another on lane 0 (the built-in host's UDP 27750 and LAN discovery are fixed), the room ones on three more lanes, each with its own lobby port and cabinet folders (`build/online-test/cabinets/A1`, `B1`, ...; Windows may ask once to let each new copy of the game through: loopback only). Keep the PC otherwise idle: a busy CPU makes real-time scenarios fail.

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
| `room` | A hosts a room with a code, B joins by code; A's voice must reach B sounding like what A said (a synthetic voice, `tests/speech.py`; noise suppression rightly stops tones), B's silent microphone must send nothing |
| `browse` | B opens the browser and picks A's room |
| `mute` | A mutes mid-game: no voice frame may arrive after it |
| `voicenet` | Voice through 5% loss and stalls (error correction and concealment) |
| `duplex` | Both talk at once: each must hear the other despite the echo safeguard |
| `ptt` | A uses push to talk and talks all along, holding the key from 1500 to 2400: B hears A then and only then |
| `echo` | B's microphone hears B's own speaker (the game and A's voice) 60 ms late at 0.8, and B says nothing: B may send nothing once the echo canceller has had 10 s, and under 150 frames in all (measured 85, in the first 3 s; 1514 without the canceller) |
| `roomleave` | B leaves the room mid-game and rejoins by code |
| `p2join` | Linked first; A starts LINK PLAY, B puts coins in at 3000 (held, then paid in) and takes LINK PLAY on the right cabinet |
| `modescreen` | A, credited with no room, finds SOLO/LINKED open (the stand-in from power-on) and takes it: the game starts at once and a room opens, no restart; B joins a while into A's game, restarts once for its handshake, and joins it |
| `menutimeout` | A, credited, lets SELECT GAME MODE run out: back to the intro with 8 coins kept; A's trigger brings the screen back and A takes SOLO/LINKED (a room opens); B joins a while into A's game |
| `menulinked` | The same with B linked from the start: A restarts with a real partner linked, and B still joins its game |
| `dropin` | A hosts and takes SOLO/LINKED alone at once (the stand-in); B joins a minute into A's game, restarts once, and takes LINK PLAY with no agreed-start wait |
| `p1return` | After `dropin`, A (P1) restarts out of the game and rejoins: it is P1 again (the server gave it slot 1), starts a new game of its own and B's goes on, with no link errors |
| `p2return` | B (P2) restarts out of the game and rejoins: P2 again, and it takes LINK PLAY into A's game once more |
| `credit` | The drop-in with "Join with a credit" on for B, which never puts a coin in: one credit goes in for B after its handshake restart, and B takes LINK PLAY with it |
| `sessionlimit` | The server's session limit set to 1 minute ends the session under a LINK PLAY game (both cabinets in it): both games go on |
| `p1over` | P1 (one credit) fights until its game is over while P2 hides and lives on; the session ends three times after: the stand-in stays out and P2 plays to its own end |
| `p2continue` | P1's game over, then P2 fights, dies and continues with its own coins (its credits go down) and plays on |
| `newcomer` | A leaves for good and joins the room afresh while B (P2) stays: B keeps P2, A takes P1 although the server gives it slot 1 |
| `persist` | A starts with `online.cfg` remembering room 1 as P1 (an app restart); B opens room 1 in slot 0. A rejoins as P1 and B, with no claim, takes P2 |
| `crossplay` | Our cabinet (P1, hosting a room) against **upstream's released desktop game** (P2, headless, joining by quick match). It checks the link from both sides, 0 link errors, upstream's agreed start holding and releasing on our trailer, and that a desktop partner is not shown voice chat. Skipped without the download |

Test switches, read by `quest/tc2_online.c` and `quest/tc2_voice.c`:

- `TC2_NET_SCRIPT="600:hostroom;2400:mute"`: actions run at those arcade frames, or `t<ms>:` milliseconds after start.
  - Actions: `host`, `join=<addr>`, `discover`, `quick`, `hostroom`, `joincode=<code>`, `browse`, `pickroom`, `rejoin`, `restart` (RESTART GAME, leaving first when online), `mute`, `talk=1|0` (push to talk's key), `voice=0|1`, `leave`, `freeze=<ms>`, and the menu's `menu=<tab>`, `resume`, `restartgame`, `restartapp`.
  - On a headset, `online-test.txt` beside the settings (written with `adb run-as` on this debuggable build) holds `script=...`, `status=1` and `voice_out=<file>`.
- `TC2_NET_STATUS=1`: a status line every 300 frames.
- `TC2_NET_ROOM_CODE=KXQ7`: a fixed code for hosted rooms.
- `TC2_VOICE_IN=<wav>`, `TC2_VOICE_OUT=<wav>`: 48 kHz mono 16-bit files in place of the microphone, and a recording of what is played.
- `TC2_VOICE_ECHO=<gain>,<ms>`: the microphone also hears the cabinet's own output, as a speaker would. `TC2_VOICE_DSP=0|1|2`: no clean-up, echo cancellation only, noise suppression only.
- The scenarios write `join_credit=0` to both cabinets' `online.cfg` (they put their coins in themselves); `credit` turns it on for B.
- `tests/test_voice_dsp.py` checks the clean-up alone, in seconds (`tests/voice_dsp_fixture.c`, above).
- `TC2_NET_DEBUG=1`, `TC2_NET_SIM=loss%,stall_ms`: the library's own logging and simulated bad network.
- `TCVR_TEST_MENU=4@60`: open the menu on its ONLINE tab at desktop frame 60 (0 GUN, 1 PLAYER, 2 GRAPHICS, 3 AUDIO, 4 ONLINE).
- `TCVR_RAMWATCH=2CE92B,2F3930`: log each main-RAM byte (hex offsets) whenever it changes.
- `TC2_NO_MODE_MENU=1`: a shot at the crossed-out SOLO/LINKED does nothing, as at the arcade (`late` links into a game on purpose).
- `TC2_MENU_TIMEOUT=1`: SELECT GAME MODE chooses by itself when it runs out, as at the arcade.
- `TC2_NO_STANDIN=1`: no stand-in partner (SOLO/LINKED then needs a linked partner, crossed out otherwise).
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
- **15-minute server sessions:** servers end a session after 15 minutes (`--max-session-min`, default 15). The lobby re-links within a few frames and the arcade link is kept up meanwhile.
  - Found in a real game (2026-11-09): P1's game was over when the limit came round, and the stand-in (an idle right cabinet that lets P1 host before anyone joins) took P2's place for 14 frames until the next session began. P1's arcade saw P2 go idle and come back mid-game, and both cabinets ended up on SELECT GAME MODE.
  - Now the stand-in stays out while the partner is still in the room. Tests run the server with a 1-minute limit: `sessionlimit` (both in LINK PLAY), `p1over` (P1's game over, P2 plays through three session ends) and `p2continue`.
- **Same wire format:** players need this build or an upstream namco22-decompile 0.9.3+ desktop game; the lobby checks game id, version and frame size.
- **The agreed start:** our own tests also saw P2's kills count when P2 started first, but the hold is upstream's measured rule and both builds apply it before P1's first game. After that, P2 plays on alone (above).
- **SELECT GAME MODE's top panel has no border on one side.** This is the arcade's own artwork, not a rendering fault. P1's SOLO/LINKED has no right edge: its top and bottom bars run to x 631 of 640 and fade to red, with the arrow pointing right. P2's LINK PLAY is the mirror image, open on the left with the arrow pointing left. On the twin cabinet the two screens stood side by side, and the two panels met as one banner across them. In VR you only see your own cabinet's screen, so the open side looks cut off. (SOLO ONLY, which is not linked, has a border all round.) When you shoot SOLO ONLY, the game dims SOLO/LINKED and slides it off the right edge.
  - *Possible enhancement:* while SELECT GAME MODE is up, draw the whole banner: the partner cabinet's mirrored half beside your own screen in VR. On the desktop, a wider view could show it too. The other half is the same model drawn mirrored, so it could come from the game's own graphics. The port would add the second copy itself: the game draws only its own half.
