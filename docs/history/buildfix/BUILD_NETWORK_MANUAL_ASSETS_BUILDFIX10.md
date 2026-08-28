# Project Titan RT1 BuildFix10 — Manual Asset Pipeline Hardening

BuildFix10 hardens the BF9 network-decoupled Asset Service around one invariant:

> Reaching `ONLINE` is the end of the network connection path, not the beginning of asset traffic.

No Asset DNS/TCP/TLS worker is created by Wi-Fi/RJ45 connection alone. Manual asset work begins only after the user explicitly requests it; automatic downloads remain disabled by default.

## Manual pipeline

Manual sessions are strictly serialized and preserve phase ordering:

1. selected game, then the remaining visible games, then the rest of the catalog;
2. probe/download icons for the whole ordered catalog;
3. only after icons finish, probe/download backgrounds;
4. one remote asset at a time: DNS -> TCP -> TLS -> download -> close -> decode -> cache publish -> yield.

The current Titan catalog does not yet expose a persistent favorite bit. The queue builder therefore leaves a documented insertion point for favorites between visible games and the rest of the catalog without changing the downloader state machine.

## Foreground responsiveness

Asset traffic remains low-priority work. Package Installer, System Update, Titan App foreground execution, foreground Storage Scheduler ownership, an active Library scan, or loss of automatic-idle eligibility prevents asset work from competing with foreground activity.

Manual sessions are intentionally not subject to the automatic 30-second delay: once the user presses X and Network Core already reports `ONLINE`, the session may start immediately.

Cache writes remain limited to 8 KiB Storage Scheduler quanta and publish through `.part -> .timg` atomic replacement.

## Automatic mode

Automatic asset downloads remain **disabled by default**. If explicitly enabled they require all of the following:

- Network Core continuously `ONLINE` for at least 1800 shell frames (~30 s at 60 Hz);
- Home is the active idle surface (no top menu/folder overlay);
- no user button activity for the automatic idle window;
- no Package Installer, PUP/System Update, Titan App foreground execution, or foreground storage ownership.

Any user activity makes automatic work ineligible immediately. The current transfer's cancellation callback observes that state, so automatic asset work yields instead of making the shell wait.

## Flight Recorder boundary

BuildFix10 adds named fine-grained asset events to text exports:

- `ASSET_SESSION_BEGIN` / `ASSET_SESSION_END`
- `DNS_BEGIN` / `DNS_END`
- `TCP_BEGIN` / `TCP_END`
- `TLS_BEGIN` / `TLS_END`
- `DOWNLOAD_BEGIN` / `DOWNLOAD_END`
- `DECODE_BEGIN` / `DECODE_END`
- `CACHE_BEGIN` / `CACHE_END`
- `AUTO_PAUSE` / `AUTO_RESUME`

This creates a clean hardware-debugging boundary: a PS2 may remain ONLINE with no Asset Service traffic, then a manual session can be started separately and diagnosed stage-by-stage.

## Preservation

- `kernel/src/services/titan_network.c` is byte-for-byte unchanged from BF9.
- The frozen official bootloader tree is byte-for-byte unchanged from BF9/BF6 baseline.
- No theme, installer, PUP, VMC, game-launch, AR9271/IOP, or Network Core behavior is changed by BF10.
