# Titan Disc Service Architecture — BuildFix30 CP03

The Disc Service is the single Titan owner of shell-time physical CD/DVD discovery. It never performs drive I/O from the frame/render path. `TitanDiscService_UpdateMainThread()` only consumes worker results and schedules a bounded 750 ms probe. Actual `sceCdGetDiskType`, readiness checks and `SYSTEM.CNF` reads run through Titan's existing serialized storage worker.

States: NO_MEDIA, INSERT_DETECTED, PROBING, MEDIA_PRESENT, IDENTIFYING, READY, UNSUPPORTED, ERROR, LAUNCHING, RUNNING, EJECTED. Event transitions are logged without notification spam. Stable media identity preserves `media_generation`; eject/swap invalidates prior title, boot path and metadata.

Initial target scope is authenticated/original PS2 CD and PS2 DVD media. Other disc types are UNSUPPORTED rather than guessed. `SYSTEM.CNF` parsing accepts BOOT2/BOOT, VER and VMODE with whitespace, casing and CRLF/LF variation. Boot paths are normalized to validated `cdrom0:\\...;1` paths; traversal, alternate devices and arbitrary colon paths are rejected. Title ID and region are inferred only when structurally supported; otherwise region is UNKNOWN.

Metadata is non-blocking. CP03 always provides an elegant Title-ID fallback; richer local/network metadata is layered above this service and must never gate launch.
