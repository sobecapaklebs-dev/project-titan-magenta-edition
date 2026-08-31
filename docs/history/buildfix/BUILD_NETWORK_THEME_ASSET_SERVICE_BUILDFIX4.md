# RT1 BuildFix4 — Titan Asset Service

BuildFix4 adds asynchronous PS2 game artwork enrichment on top of Network/Theme RT1 BuildFix3. The official USB BDM BuildFix6 bootloader remains unchanged.

## Runtime rules

- Home and Library render from RAM only. No HTTP, FileXio, PNG/JPEG decode or cache scan runs in the render/input path.
- Every game tile visible in the main Home/Library grid keeps an icon: cached artwork when ready, otherwise the existing Titan placeholder immediately.
- Selected icon is P0; other visible icons are P1.
- Selected background waits 10 stable frames before becoming eligible, preventing fast left/right navigation from streaming unused backgrounds.
- After 300 stable frames, idle enrichment walks the remaining catalog, icons before backgrounds.
- One network transfer is active at a time on a low-priority EE worker.
- Persistent storage consumes at most one 8 KiB quantum per Asset Service turn and alternates fairly with Library scan.
- Package installation, PUP update, Titan App exclusive mode, VMC/foreground storage and urgent audio retain priority over assets.

## TIMG v1

The gateway emits decode-free TIMG payloads already in GS_PSM_16:

- icon: 32x32, 2,048-byte payload;
- background: 256x128, 65,536-byte payload;
- 32-byte header;
- CRC32 over payload;
- flag 1 = native GS_PSM_16.

This removes runtime image resize and RGB565-to-GS repacking from the R5900. Native 720p remains at the existing exact 4 MiB VRAM high-water mark, so the renderer reuses the pre-existing 256x64 scratch/cached-layer paths rather than allocating another resident GS texture.

## Cache

- `mass:/TITAN/cache/assets/<SERIAL>.icon.timg`
- `mass:/TITAN/cache/assets/<SERIAL>.bg.timg`

Writes use `.part` followed by `TitanStorageIo_AtomicReplace()` only after a complete validated TIMG is available.

## Asset Gateway

`tools/titan_asset_gateway.py` is the development gateway. The PS2 talks plain HTTP only to this configured gateway; the gateway performs provider HTTPS access, image decode/resize, conversion to GS_PSM_16 and its own host-side cache.

Provider order in BF4:

- icon: local override -> Luden02 OPL Manager `_COV.png` -> `_COV2.png` -> xlenore `ps2-covers`;
- background: local override -> Luden02 OPL Manager `_BG_00.png` -> blurred/darkened cover fallback.

No provider key or third-party artwork is embedded in `kernel.elf` or redistributed in the source package.

## Hardware status

This BuildFix is host-validated only. The current environment does not contain the PS2 cross-toolchain, so final PS2SDK link/build and physical PS2 behavior must be verified on the user's Garuda/PS2DEV machine. BuildFix4 also inherits the pending physical validation of RT1 BuildFix3's post-black-screen network startup changes.
