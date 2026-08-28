# Titan Asset Service — RT1 BuildFix5

## Invariant

Asset activity is never allowed on the shell render/input critical path. A tile
is always drawable immediately: cached art if resident, otherwise the existing
Titan placeholder. Network, image decoding and storage publication run outside
the renderer.

## Direct Internet path

No PC gateway and no `assets.cfg` are required. Once Titan Network Core reports
ONLINE, the low-priority Asset Service resolves the PS2 startup serial and tries
public providers directly over HTTP/HTTPS through PS2IP.

Current provider order:

- icon: Luden02 `_COV.png`, Luden02 `_COV2.png`, xlenore cover JPEG;
- background: Luden02 `_BG_00.png`, Luden02 `_SCR_00.png`, xlenore cover fallback.

The runtime normalizes `SLUS_209.46` to `SLUS-20946` for the cache/provider key
and reconstructs the OPL form when a provider uses it.

## Native HTTPS

`titan_http_client.c` is a bounded HTTP/1.1 client. HTTPS uses BearSSL with SNI,
hostname verification and a pinned build-time CA bundle. The first developer
build fetches BearSSL 0.6 and the pinned trust bundle; these are build inputs,
not a runtime gateway.

## Decode pipeline

Remote PNG/JPEG is downloaded into one transient compressed-source buffer by the
asset worker. `titan_asset_image.c` decodes one source row at a time and writes
the final small GS PSM16 asset directly:

- icon: 32x32 PSM16;
- background: 256x128 PSM16.

The source image is released immediately after conversion. TIMG contains a small
header plus CRC32 and ready-to-upload PSM16 payload.

## Scheduling

- P0: selected game icon;
- P1: every icon currently visible in Home/Library;
- selected background: only after 10 stable frames (~166 ms at 60 Hz);
- P4: rest of catalog only after 300 stable frames (~5 seconds).

Only one network transfer runs at a time. The worker yields between bounded
network chunks. It cancels/requeues work when the network goes offline, Package
Installer or System Update becomes active, Titan App runtime owns foreground
resources, the Storage Scheduler reports foreground work, or a background is no
longer for the selected game.

Storage remains serialized through TitanStorageScheduler in <=8 KiB quanta.
Downloads publish as `.part` -> CRC-valid TIMG -> atomic replacement.

## Cache

`mass:/TITAN/cache/assets/`

The renderer performs no socket, HTTP, filesystem or PNG/JPEG work. Cached tiles
remain visible off-focus; missing tiles use the normal placeholder until their
asset is committed on the main thread.

## Build dependencies

The developer PS2SDK needs its EE ports for zlib, libpng and libjpeg-turbo.
BearSSL is built locally for R5900 by `kernel/third_party/bearssl_ps2/Makefile`.
Normal `make clean` does not delete the downloaded TLS build cache.
