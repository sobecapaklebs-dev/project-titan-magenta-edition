# BuildFix26 Memory Budget / Watermark Model

## GS VRAM
Physical budget remains exactly 4 MiB. BuildFix25 reserved-headroom policy and the 720p Reference default are unchanged. Boot snapshots retain used/free/reserved/framebuffer/texture/theme/font/CLUT/transient/residency/failure/reclaim/eviction counters. BuildFix26 adds target-memory fields to the same phase history without per-frame I/O.

## EE RAM
On EE builds, BuildFix26 samples the current program break with `sbrk(0)` only at explicit capture points. The value is an EE memory-use estimate relative to 0x00100000..0x02000000, matching the conservative model already used by DEX. It now tracks current used/free estimate, peak used and minimum free. It is NOT GS VRAM and is never added to GS counters.

Fragmentation is not claimed because the current runtime has no authoritative max-free-block query wired into the EE application. Global malloc failures also cannot be inferred; the counter records only failures explicitly reported to the service.

## IOP RAM
No EE-side authoritative IOP free-memory backend exists in the current source. Therefore `iop_ram_valid=0` in production until a target-measured backend is implemented and cross-built. BuildFix26 deliberately reports unknown instead of fabricating bytes. A separate known-resident-module count can be supplied by service owners.

## Validation status
HOST_VALIDATED: watermark arithmetic + validity semantics + sanitizer harness.
TARGET_BUILD_VALIDATED: NOT_EXECUTED (R5900/PS2SDK/dvp-as unavailable in this environment).
HARDWARE_VALIDATED: NOT_EXECUTED.
