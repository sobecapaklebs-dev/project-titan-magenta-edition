# wLaunchELF — Project Titan native port

This is the first native-port milestone for wLaunchELF inside Project Titan.
It is a `.tmod`, not an externally executed wLaunchELF ELF. Project Titan keeps
ownership of rendering, input, storage clients, IOP state and ELF handoff.

## Implemented in milestone 0.1.0

- Titan-native device browser for `mass:/`, `mc0:/`, `mc1:/`, `cdfs:/` and `hdd0:`.
- Paged directory enumeration through the append-only Titan App API v1 extension.
- File/directory metadata.
- Cooperative 16 KiB file copy so long copies advance from `titan_app_update()` rather than monopolizing a standalone event loop.
- Empty-directory/file deletion through Titan storage policy.
- `.elf` launch through Titan's validated stage2 handoff.
- Native Titan drawing/input/notifications and R3-owned window/fullscreen behavior.

## Deliberately not carried over yet

The original wLaunchELF directly owns FileXio/IRX setup, PFS mounts, VMC mounts,
FTP/network state, gsKit drawing and pad loops. Those are not copied into the
Titan process. HDD/PFS write management, recursive folder copy, text editor,
JPG viewer, rename/new-folder UI, FTP and VMC management remain follow-up ports
that must be expressed through Titan services.

The next upstream-import step is to vendor the reusable algorithms from
`filer.c`, `elf.c`, `hdd.c`, `editor.c` and related files behind this adapter,
with AFL-2.0 attribution preserved.
