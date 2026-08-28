# BuildFix27 Checkpoint CP39
STATUS: HOST_VALIDATED / PFS_IOP_DRIVER_BLOCKED
PREVIOUS_VALID: CP38
BLOCK: concrete PS2 FileXio PFS platform hooks + official provenance

- Verified official PS2SDK FileXio/PFS interfaces at commit a13b5971ec0e39c7ba8b8559b80a4e81c8425352.
- Internal provider mount now receives capabilities, preserving RW/RO semantics end-to-end.
- Added PS2 hooks using fileXioGetDeviceList, fileXioMount and fileXioUmount.
- Provider is available only when both hdd and pfs devices are reported by FileXio.
- Internal pfsN:/ roots are normalized to FileXio pfsN: only at the platform boundary.
- WRITE capability maps to FIO_MT_RDWR; read-only maps to FIO_MT_RDONLY.
- No PFS IRX is loaded or embedded because no built/materialized target module exists in this workspace.

NEXT: CP40 exact PFS IOP driver provenance/materialization gate.
