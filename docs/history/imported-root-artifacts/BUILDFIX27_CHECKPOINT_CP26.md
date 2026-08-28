# BuildFix27 Checkpoint CP26
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP25
PARENT_ARCHIVE: BUILDFIX27_CP25_INCREMENTAL.tar.gz
BLOCK: native-app FTP lifecycle/status + wLaunchELF integration

- TitanAppApiV1 remains v1 append-only and adds ftp_start/ftp_stop/ftp_status after the previous media-image tail.
- Apps receive no TCP/socket handle; FTP remains kernel-owned Network Stream + Storage/VFS.
- Runtime validates FTP root through writable StoragePathSafe policy, ticks FTP cooperatively while the app is active and forcibly stops it during app destruction.
- wLaunchELF toggles FTP for the current writable directory with L3; R3 remains Titan-reserved.
- wLaunchELF gates struct_size through the actual final field it consumes (`ftp_status`).
- Kernel/wLaunchELF/Project NES SDK copies are byte-identical.
- App Runtime strict ASan/UBSan ET_REL/R5900 guard harness, ABI fixture, wLaunchELF and Project NES host suites pass.

NEXT: CP27 SDK/documentation + reusable native-app integration guide/contract.
