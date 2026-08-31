# Project NES Titan Port Report — BuildFix27

## CP08 — core/frontend separation

Status: HOST_VALIDATED boundary; FCEUmm provider import BLOCKED by source-byte materialization in this environment.

### Selected core family
FCEUmm remains the selected family from CP07. The fixed references are:
- libretro/libretro-fceumm commit `b5e3566515c27dc66c9c20572171673126532e06` (functional/core reference)
- ps2homebrew/Fceumm-PS2 commit `dc77e3ca4baad08f0023f61dba9846550735b74d` (R5900/backend reference)

No FCEUmm source bytes have been copied into Project Titan in CP08. The container has no external DNS and the connected GitHub interface exposes repository files/commits but not an archive that can be materialized into the build filesystem. The Library also contains no proven FCEUmm source snapshot. Therefore claiming a core replacement here would be false.

### Material change
Project NES now has a small `ProjectNesCoreBackend` boundary for load/reset/input/frame execution. The existing NROM implementation is an explicit provider named `Legacy NROM fallback`. Frontend call sites route through the provider; the provider itself calls the pre-existing emulator functions directly. This prevents another accidental frontend/core entanglement and creates the insertion point for the selected FCEUmm provider once exact source bytes are available.

### Invariants
- Project NES identity/frontend remains Titan-native.
- Titan App Input remains system-owned; SELECT+CIRCLE is still reserved by the runtime.
- Existing Titan media/video/audio/storage ownership is unchanged in CP08.
- Legacy mapper-0 provider is fallback only; it is not re-described as the selected mature core.
- No commercial ROM is included.
- CP09+ must not claim FCEUmm integration until a provider sourced from the fixed upstream bytes is actually present.

## CP09 — Titan Video/Input boundary

Status: HOST_VALIDATED. The provider-facing input type is now NES-domain data (`ProjectNesCoreInput`) rather than `TitanAppInput`. The frontend owns SELECT+CIRCLE interception and translates application-visible controls. The provider-facing frame operation receives an explicit RGB565 destination/geometry; Titan presentation remains exclusively in the frontend through `media_present_rgb565`. This is the contract the future FCEUmm adapter must satisfy without allocating GS VRAM directly.

## CP10 — Titan Audio/Timing boundary

Status: HOST_VALIDATED. The provider produces interleaved S16 stereo PCM into a frontend-owned buffer and reports its frame rate in millihertz. Titan alone owns queue availability/submission/start/stop. Under backpressure the legacy provider does not advance its audio cadence slot. The frontend scheduler now uses the provider rate instead of a hardcoded 60 Hz value, with a conservative 40–70 Hz validity guard. This is sufficient for a future FCEUmm PAL/NTSC adapter without exposing Titan audio APIs inside the core.

## CP11 — Titan Storage/Saves + lifecycle

Status: HOST_VALIDATED. Battery-backed PRG RAM is now an optional provider capability instead of a Titan-storage dependency inside the emulator core. The active Legacy NROM fallback recognizes the iNES battery flag and exposes 8 KiB PRG RAM import/export/dirty callbacks through `ProjectNesCoreBackend`.

The Titan frontend derives a deterministic per-ROM sandbox path under `data/saves/`, restores exact-size SRAM before provider reset, periodically flushes dirty SRAM, and flushes again on pause/reset/shutdown. Publication uses the existing `TitanAppApiV1.file_write_data` path, whose runtime implementation already stages and atomically replaces the sandbox file. Dirty state is cleared only after a complete successful write; a failed lifecycle flush warns and leaves the provider dirty for a later retry.

No trusted VFS handle, FileXio ownership, or Titan API type was added to the core boundary. ROM-free host fixtures cover battery SRAM import/export/dirty state and shutdown persistence. FCEUmm remains selected but NOT_MATERIALIZED; this checkpoint does not claim mapper expansion or core replacement.

## CP12 — ROM-free mapper/region/SRAM/audio conformance + provider readiness

Status: HOST_VALIDATED. Added `ProjectNesCoreRomInfo` and a provider-neutral ROM probe so mapper/format/region/battery/trainer metadata can be inspected without coupling the frontend or core boundary to Titan services. Classic iNES byte counts are normalized; NES 2.0 is identified, including 12-bit mapper/submapper and timing hint, while complex NES2 ROM-size encoding is deliberately left unclaimed by the common probe.

The Legacy NROM fallback now returns explicit unsupported-mapper/unsupported-format results instead of conflating metadata recognition with execution support. A generated ROM-free conformance fixture proves mapper 0/1/0x123 decoding, PAL hints, battery SRAM visibility, Legacy rejection of mapper 1/NES2, the 60.000 Hz provider rate, and the 533/533/534 PCM cadence that totals 3200 stereo frames over six video frames at 32 kHz.

This is readiness work, not a mature-core claim: FCEUmm is still NOT_MATERIALIZED and Legacy NROM remains the only active provider.

## CP13 — repeated lifecycle/fault cleanup + provider/package readiness closure

Status: HOST_VALIDATED. The Project NES lifecycle harness now executes ten complete init/load/run/shutdown cycles with guarded allocations. The first cycle deliberately fails an SRAM publication during pause; the provider dirty flag remains set, resume succeeds, and shutdown retries the same dirty SRAM successfully. ASan/UBSan with leak detection passes the ten-cycle harness.

Packaging truthfulness is now explicit in the Project NES README: the active package still contains only the original Legacy NROM fallback and keeps its current MIT license. The selected FCEUmm provider is not materialized or linked. A future distributed combined module that actually links FCEUmm must carry the applicable GPL-2.0 obligations instead of being presented as an MIT-only emulator module.

With CP08–CP13, the Titan frontend/provider boundaries for video, input, audio/timing, saves, ROM metadata and cleanup are host-validated. Mature FCEUmm replacement remains externally blocked by exact source-byte materialization and later R5900/target proof; no false completion is claimed.

## CP14 — Browser / Titan Network Client ownership boundary

Status: HOST_VALIDATED. Browser transport no longer calls PS2IP/lwIP DNS/socket primitives directly. A reusable owner-aware `TitanNetworkClient` now arbitrates the existing bounded Titan HTTP/HTTPS transport between Browser and Asset Service. Browser requests carry explicit cooperative cancellation and a 60 s deadline, while completed download publication remains routed through Titan Storage atomic replacement. The lower HTTP/TLS layer retains bounded `select()` waits and certificate/hostname validation. This checkpoint does not expose AR9271/Ethernet internals to apps and does not claim a second network stack.
