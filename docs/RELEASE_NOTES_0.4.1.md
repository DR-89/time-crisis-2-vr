# Time Crisis II VR v0.4.1

This update fixes a Crossplay disconnect caused by temporary stalls of the game thread. It includes the online mode and the in-game server editor with lowercase/uppercase input from v0.4.0.

## Crossplay fix

The lobby removes a client after five seconds without traffic. In v0.4.0, a stalled game thread also stopped the connection's keepalive messages. The upstream desktop partner continued to occupy the running room, which refused the evicted client's attempts to rejoin. A controlled 6.5-second stall reproduced that failure.

A background timer now sends a copied PING once per second while normal traffic is stalled, for at most 30 seconds after the last main-thread send. The timer never runs emulation, sends game frames or reads incoming packets. Disconnects and server changes clear the protected socket snapshot before closing it, and stop the timer. The wire protocol and default server remain compatible with upstream 0.10.1.

The Crossplay regression test now covers the stall explicitly. It also waits for the actual room to open, isolates upstream instances and always cleans up child processes. The upstream test partner receives a longer run budget so that its normal exit cannot be mistaken for a connection failure.

## Downloads and updating

- **TimeCrisis2VR-v0.4.1-quest3.apk**: Quest standalone, version code **13**. Uses the same application ID and signing certificate as official v0.4.0/v0.3.3. Install with `adb install -r TimeCrisis2VR-v0.4.1-quest3.apk` to preserve settings and cabinet data.
- **TimeCrisis2VR-v0.4.1-windows-x64.zip**: extract the complete archive. Launch **Play VR.cmd**, **Play SteamVR.cmd** or **Play Desktop.cmd**. Keep your existing `online.cfg`, `quest-options.cfg` and cabinet data when updating.
- **SHA256SUMS.txt**: hashes of both packages.

Both packages include the game content, weapon models and attribution, as in the previous release.

## Validation

- Crossplay with the released upstream 0.10.1 desktop game passed both normally and with a forced 6.5-second main-thread stall. Each run completed 6,600 frames on the VR port, preserved the link and agreed start, and reported zero game-link errors, CPU traps or floating-point exceptions. During the stall, six background PINGs preserved the lobby membership.
- The lobby-room/voice test and the leave/rejoin test passed with two Windows clients. All four integration scenarios passed in one serial run.
- Deterministic UDP tests passed packet identity, send cadence, normal-traffic suppression, server/room changes, grace-period expiry, clock wrap and disconnect checks. The server-editor validation and persistence regression also passed.
- The Windows ZIP passed a fresh-extraction check, internal hashes, model attribution, desktop OpenGL rendering and 1,501 frames. The Quest APK passed ARM64/DEX, immersive manifest, icons, game/model hashes, signature and 16 KB ZIP-alignment checks.

## Limitations

These remain experimental ports. No new Quest or PCVR headset playthrough was performed for these exact binaries. The keepalive protects temporary main-thread stalls while the process is running; it cannot prevent an actual network outage or suspension of the entire process. The previously reported stage-two boat-boss issue in linked play is not addressed by this update.

[Online guide](https://github.com/DR-89/time-crisis-2-vr/blob/v0.4.1/docs/ONLINE.md) · [Controls and setup](https://github.com/DR-89/time-crisis-2-vr/tree/v0.4.1#readme) · [Attribution](https://github.com/DR-89/time-crisis-2-vr/blob/v0.4.1/NOTICE.md) · [Full changelog](https://github.com/DR-89/time-crisis-2-vr/compare/v0.4.0...v0.4.1)
