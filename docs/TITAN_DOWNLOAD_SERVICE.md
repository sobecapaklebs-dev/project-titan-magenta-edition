# Titan Download Service — BuildFix29

Titan Download Service is the single reusable package-download pipeline for Store, Browser handoff and future first-party catalog clients.

## Pipeline

`QUEUE -> HTTP/HTTPS STREAM -> .part -> SHA-256 -> TPKG STRUCTURAL VALIDATION -> ATOMIC PUBLISH`

The HTTP/TLS worker never owns FileXio. It places bounded 4 KiB body chunks into a four-slot fixed ring. Titan's existing serialized storage worker drains one chunk at a time, preserving the platform's single-owner storage model and allowing Audio QoS to preempt between quanta.

The service uses no per-frame heap allocation. `TitanDownloadService_Update()` is intentionally metadata-only; no network or filesystem operation is performed from the shell frame path.

## Queue and status

The fixed queue supports six jobs. Snapshots expose state, bytes downloaded, total bytes when known, percentage, measured transfer speed, ETA when reliable, retry count and final failure reason.

Cancel is cooperative. Pause is supported, but BuildFix29 intentionally reports `resume_supported=0`: TitanHttpClient does not yet have an audited HTTP Range/Content-Range contract, so resuming a paused active transfer restarts from byte zero rather than pretending a partial file is safely resumable.

## Integrity

Repository metadata must provide expected package size and SHA-256. A transfer is not published unless:

1. the HTTP request completes successfully;
2. expected size matches when known;
3. SHA-256 matches;
4. Package Installer accepts the TPKG structure;
5. `TitanStorageIo_AtomicReplace()` publishes the validated temporary file.

Failed or cancelled temporary files are removed. A previous published package is retained via the storage atomic-replace backup path until the new package is known-good.

## Target validation

Host contracts validate queue/state/integrity semantics under ASan+UBSan. PS2 network/storage timing and real repository downloads remain target/hardware validation items until PS2DEV/PCSX2/physical hardware are available.

## BF29 platform-closure idle cost

The service is lazy. `Initialize()` does not allocate the six-job queue, streaming ring, worker stack or create the EE worker thread. Those resources are allocated once on the first accepted enqueue and released by bounded shutdown. `Update()` is bookkeeping only and performs no network or storage I/O. This keeps an unused Download Service close to zero recurring cost.
