# On-disk and launch contracts

## Profiles

`TITAN.DAT.tmp` is written and re-read first. The current `TITAN.DAT` becomes
`TITAN.DAT.bak`, the verified temporary file is published, and recovery checks
all three files on boot. The only valid copy is never truncated in place.

## VMC

VMC creation writes a formatted image once, validates superblock, IFC/FAT/root
structure and expected length, then publishes through `.new`/`.bak`. Recovery
runs before a VMC is reported ready.

## Library cache

Records are encoded field-by-field with fixed widths and checksums rather than
raw C structs. Strings, enums, paths and counts are validated before publish.
A partial scan, unplug, truncated UL record or cache write failure cannot replace
a previously valid catalog.

## Installed applications

TPKG v1 files are read from `mass:/TITAN/packages`. Extraction uses
`mass:/TITAN/apps/.install-TITLEID`; only a package whose header, table, every
file CRC32 and MIPS ELF entry point validate is renamed to
`mass:/TITAN/apps/TITLEID`. The generated `titan_app.ini` makes the application
discoverable by the library and Home grid. Cancellation or failure removes the
hidden staging directory. See `TITAN_PACKAGE_FORMAT.md` for the binary layout.

## Launch arguments

Titan Neutrino Engine receives ISO path, working directory and active VMC.
Titan UL Engine receives startup, `titan-ul`, and `-vmc0=<active profile VMC>`.
Both external ELF paths are validated before stage2 handoff.

## Installed themes

Installable `.tthm` packages are TPKG v2 `theme` packages. After validation they are
published under `mass:/TITAN/themes/<THEME-ID>/`. `theme.cfg id=` must match the package
TPKG title ID. Build 36 supports Theme Shell API v3; full authoring and scene/idle-loop
format details are in `docs/THEME_AUTHORING.md`.
