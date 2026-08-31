# BuildFix27 Checkpoint CP25
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP24
PARENT_ARCHIVE: BUILDFIX27_CP24_INCREMENTAL.tar.gz
BLOCK: cooperative FTP RETR + transactional STOR

- Network Stream now exposes an internal EOF-aware ReadStatus while legacy Read remains compatible.
- RETR reads bounded 4 KiB chunks through TitanStorageIo_ReadSome and sends via opaque data stream.
- STOR writes only to `<target>.titantmp`; remote clean EOF is required before TitanStorageIo_AtomicReplace publishes the destination.
- Failed/cancelled upload removes temporary data and never deliberately truncates the known-good target.
- FTP data/control/list regressions and Network Stream stale-handle/timeout/cancel tests pass.
- wLaunchELF still owns no sockets/FileXio.

NEXT: CP26 append-only native-app FTP lifecycle/status API + wLaunchELF UI integration.
