# Project Titan RT1 BuildFix11 — Asset Subsystem Hardening

BuildFix11 audits the complete downloaded-asset path after the physical-PS2 BF10 Flight Recorder showed a successful DNS/TCP path followed by state corruption during `TLS_BEGIN`.

## Memory / TLS worker

- Moves the HTTP/BearSSL scratch state out of the Asset Network Worker stack.
- Moves the BearSSL bidirectional I/O buffer into isolated static HTTP scratch.
- Moves the worker stack out of `TitanAssetServiceState` so a stack overrun cannot overwrite `storage_job`, current asset indices, flags, or diagnostics state.
- Raises the isolated worker stack to 48 KiB and surrounds it with `TITN` canaries plus fill-pattern watermark telemetry.
- A canary failure disables further automatic/manual Internet asset work for that boot instead of continuing with potentially corrupted state.
- Reuses one 1.5 MiB compressed-source allocation for an active asset session and releases it when the worker becomes inactive.

## HTTP / HTTPS failure containment

- Keeps the client single-flight and bounds redirects to five.
- Adds a cooperative request deadline (30 s for Asset Service), cancellation checks between bounded I/O operations, and TLS engine-error telemetry.
- Rejects HTTPS -> HTTP redirect downgrade.
- Rejects malformed URL authority/ports and control characters in paths.
- Rejects malformed status/header/body framing and oversized `Content-Length`/chunk bodies before they can exceed the destination buffer.
- Does not download HTTP error bodies as images.
- Empty or malformed HTTP-200 image payloads are treated as transient provider failures rather than permanently marking an asset unavailable.

The cooperative deadline cannot preempt a single underlying PS2IP DNS/connect/recv call while that call itself is blocked; it bounds and cancels at every boundary controlled by Titan without introducing an unproven socket API dependency.

## Decode safety / responsiveness

- PNG input callback uses subtraction-safe bounds checks.
- Rejects Adam7/interlaced PNG because the converter is deliberately a one-row streaming path.
- Caps decoded dimensions/pixel count and validates transformed PNG row size before allocation.
- JPEG dimensions/pixel count are capped before row processing.
- Adds `TitanAssetImage_ConvertToTimgEx()` with cooperative cancel/yield hooks; Asset Service yields every eight decoded rows and can abort decode for user/foreground activity.
- Failure always returns `out_size=0`; a malformed source can never publish a stale TIMG length.
- Malformed/truncated PNG/JPEG cases are covered under ASan/UBSan host testing.

## Cache / Storage Scheduler

- Every storage job is validated before index access, arithmetic, seek, read, write, or publish.
- Write jobs require the exact expected TIMG size and a valid TIMG CRC before the first byte is written.
- Read jobs require the exact cache-file size; invalid/truncated cache files are removed and safely requeued.
- Cache identity is immutable per transaction: serial, startup hash, catalog generation, owner, and manual-session generation travel with the job.
- Catalog changes drop stale results rather than attaching old art to a new game index.
- `.part` ownership remains with an active write until the job ends; cancel cannot release/reuse `download_buffer` underneath the writer.
- Network asset selection is blocked while another Asset Storage job owns the pipeline.
- Write/read failures clear their ready flags and recover/requeue instead of looping forever.
- Main-thread staging is always released after commit/reject, eliminating the BF10 permanent `storage_stage_ready` stall.
- Cache clear enumerates the asset directory itself, removing orphan `.timg`/`.part` entries one scheduler quantum at a time.

## Session / queue correctness

- Manual and automatic results are tagged with owner + session generation so a late automatic result cannot be counted by a newly started manual operation.
- Manual cache misses retry immediately in the same explicit session; automatic retries keep their backoff.
- Explicit manual runs can retry assets previously marked unavailable.
- Manual network/decode failures advance deterministically rather than hanging a phase.
- Session-end diagnostics report failure if any asset failed.
- Manual-order indices are range-checked before dereferencing game state.

## Flight Recorder additions

- `ASSET_STACK_CANARY_FAIL`
- `ASSET_STACK_MIN_FREE`
- `ASSET_STORAGE_JOB_REJECT`
- `TLS_ENGINE_ERROR`
- `HTTP_PROTOCOL_ERROR`
- `CACHE_WRITE_FAIL`
- `CACHE_LOAD_FAIL`
- `CATALOG_RACE_DROP`
- `SOURCE_ALLOC_FAIL`

The BF10 phase boundaries (`ASSET_SESSION_BEGIN`, DNS/TCP/TLS, DOWNLOAD, DECODE, CACHE, `ASSET_SESSION_END`) remain intact.

## Preservation boundary

- `kernel/src/services/titan_network.c`: unchanged from BF10.
- `bootloader/`: unchanged from BF10 / frozen official BF6 bootloader baseline.
- No theme, installer, PUP, VMC, game-launch path, AR9271/IOP driver, WPA2, NetMan, PS2IP/DHCP, or renderer layout behavior is changed by BF11.
