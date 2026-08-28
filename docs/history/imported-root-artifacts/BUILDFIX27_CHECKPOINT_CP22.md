# BuildFix27 Checkpoint CP22
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP21
PARENT_ARCHIVE: BUILDFIX27_CP21_INCREMENTAL.tar.gz
BLOCK: bounded Titan-owned TCP server stream service

Material changes:
- Added fixed-capacity Titan Network Stream service: 2 listener slots, 4 connection slots.
- PS2IP socket/bind/listen/accept/recv/send/lwip_close ownership stays inside the kernel service.
- Consumers receive generation-checked opaque handles; descriptors never cross the service boundary.
- Accept/read/write waits are deadline bounded (default 5 s, hard cap 30 s).
- Owner cancellation, stale-handle rejection, fixed capacity rejection and deterministic close/reset are implemented.
- Executable host transport fixture proves listen -> accept -> receive -> partial-send loop -> timeout -> cancel -> close.
- Existing HTTP Network Client, native-app ABI, wLaunchELF and Project NES host suites remain green.

Target status:
- R5900/VU target build remains BLOCKED by CP21 toolchain absence; no target claim is made.

NEXT: CP23 Titan-owned FTP service adapter on top of Network Stream + Storage/VFS, with no direct socket/FileXio ownership in wLaunchELF.
