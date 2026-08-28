# BuildFix27 Checkpoint CP40
STATUS: HOST_VALIDATED / TARGET_BINARY_BLOCKED
PREVIOUS_VALID: CP39
BLOCK: PFS IOP driver provenance/materialization gate

- Official PS2SDK source ref fixed at a13b5971ec0e39c7ba8b8559b80a4e81c8425352.
- APA/HDD output proven as ps2hdd.irx; PFS output proven as ps2fs.irx.
- PFS module identity proven as pfs_driver and IOMANX device `pfs`.
- Added executable target prerequisite validator; host reports PFS_TARGET_READY=0 truthfully in this environment.
- CP39 FileXio hooks remain fail closed until both hdd+pfs devices are online.
- No IRX bytes were fabricated, vendored or claimed.

NEXT: CP41 final BF27 host/material closure + target handoff refresh; external target/FCEUmm gates remain open.
