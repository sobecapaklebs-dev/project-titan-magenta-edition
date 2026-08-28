# Project Titan RT1 BuildFix9 — Manual, Network-Decoupled Asset Downloads

BF9 reimplements the direct-Internet Asset Service so a successful Wi-Fi/RJ45 connection never starts asset DNS/HTTP/TLS traffic by itself. The network may remain ONLINE indefinitely with the asset Internet worker absent.

## Settings > Network > Dados de jogos

A sixth Network row opens a dedicated game-data page with four actions:

- `Baixar assets dos jogos`: manual missing-only pass.
- `Atualizar assets existentes`: force-refresh all game icons and backgrounds.
- `Limpar cache de assets`: asynchronous cache removal through the serialized Storage Scheduler.
- `Download automático`: optional runtime toggle, **disabled by default on every boot** while hardware stability is being proven.

Starting a manual download requires the Titan Network Core to already report `ONLINE`. Cache clearing does not require Internet.

## Manual pipeline

The manual pass is deliberately serial:

1. Probe/resolve every game icon.
2. Download missing/refresh icons one at a time.
3. Only after the icon phase is complete, probe/resolve backgrounds.
4. Download missing/refresh backgrounds one at a time.

The renderer never waits for any of these steps. Visible games continue to use an already-cached icon or the existing placeholder. Published cache files remain atomic `.part -> .timg` transactions.

## Responsiveness contract

- No Asset HTTP/TLS worker is created during boot or network connection.
- The Internet worker is created lazily only by a manual download or by explicitly enabling automatic downloads.
- One network transfer is active at a time.
- Package Installer, System Update, Titan App foreground execution, or foreground Storage Scheduler ownership abort/pause an asset fetch.
- Cache writes remain bounded to 8 KiB per storage-worker quantum.
- Manual background downloads do not get cancelled merely because the user navigates to another tile.
- `X` on the progress page returns to Network while the worker continues in the background.
- `O` cancels the manual operation; already atomically published cache entries are preserved.

## Diagnostics

BF9 adds Asset Flight Recorder markers for manual-session start, phase changes, completion, cancellation, automatic-mode toggles, lazy worker creation, and individual fetch begin/end. This makes the next hardware run distinguish network connection from explicit asset traffic.

## Scope / preservation

- `kernel/src/services/titan_network.c` is byte-for-byte unchanged from BF8.
- The BF6 official bootloader tree is byte-for-byte unchanged.
- No theme, Package Installer, PUP, VMC, Library layout, or game-launch behavior is changed.
- Direct providers / BearSSL / PNG-JPEG conversion from BF5 remain available, but are dormant until explicitly requested.
