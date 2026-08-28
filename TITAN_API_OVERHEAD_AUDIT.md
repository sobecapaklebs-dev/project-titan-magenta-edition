# Titan API Overhead Audit — BF28 CP10

## Measurement model
`TitanPlatformOverhead` is a fixed-size, no-heap profiler. It is opt-in. When disabled, Begin returns before reading the clock and End returns on token zero. This preserves a near-zero-cost production path while allowing the same target binary to collect active/idle costs when DEX profiling enables it.

Domains reserved: Runtime, TGR, pikaVR, VlombisCore, VFS, Network, Session, IGR, Overlay, Unified Input, SAS, Application Runtime, telemetry, classification, Theme, Storage and Audio.

## Host measurements
Executor measurement (2,000,000 iterations, optimized host build):
- profiler disabled Begin: ~1.73 ns/call;
- profiler enabled Begin+End pair: ~52.54 ns/call.

These are HOST_MEASURED values only. They are not PS2/R5900 claims. Target values remain TARGET_PENDING and will be collected with the same counters on PS2.

## Fast-path corrections
- diagnostics p95 sorting reduced from every frame to every 8 frames;
- non-critical DEX network/asset/RAM telemetry sampled every 30 frames;
- disabled network no longer rewrites the offline detail every frame once already offline;
- Session idle update remains immediate-return;
- Overlay closed does no layout/render and only checks normalized chord state;
- pikaVR disabled/reference TryReuse path returns before registry scan;
- VlombisCore capability mask 0 returns before telemetry/logging;
- new overhead profiler disabled path performs no clock read and no allocation.

## No-frame-allocation gate
BF28 Session, Overlay, Unified Input and Platform Overhead services contain no malloc/calloc/realloc/free calls. Existing frame-path filesystem/network ownership contracts remain unchanged.
