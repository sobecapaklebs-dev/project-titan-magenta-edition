# Project Titan RT1 BuildFix15 — Asset Transport State & Recovery

- Removes the stage-blind Asset Service 20-second body-progress watchdog. DNS/TCP/TLS/header time now belongs to the HTTP client's 60-second cooperative request deadline.
- Adds structured HTTP failure/stage reporting and suppresses false BearSSL engine errors when the request was intentionally cancelled or timed out.
- Adds CDN/raw mirrors for the same semantic ICON/BACKGROUND sources; covers remain forbidden.
- Validates complete body length and PNG/JPEG magic before decode. Complete but unsupported/corrupt provider content falls through to the next provider instead of becoming a Titan network error.
- Safely supports Adam7 PNG through bounded multi-pass row combining; no full decoded source image is retained.
- Manual transient retry budget is four attempts and remains round-robin so one slow title cannot block later games.
- Storage orphan recovery no longer shares a network body-progress timer and never resets a live network transfer.
- Network Core and frozen BF6 bootloader are unchanged.
- Fixes manual phase completion to track the currently active network kind, not stale `download_kind`, and requires the network/download/storage/stage pipeline to be fully drained before `ASSET_SESSION_END`.
- Storage stall recovery is owner-safe: the main thread requests abort and the serialized storage worker performs `.part` cleanup/requeue; live jobs are never memset/removed cross-thread.
