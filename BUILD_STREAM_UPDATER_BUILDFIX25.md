# Project Titan Magenta Edition RC14 — BuildFix25

Base: BuildFix24 (Flow multi-wave, system error popup, wLaunchELF render fix).

Hardware evidence from BuildFix23 showed:

`CRC PUP invalido arq 1 exp 991AF601 obt FEB1038D`

The mismatch happens in the read-only preflight pass, before any staging write. That rules out staging corruption as the primary cause and points to payload positioning/seek behavior on the PS2 USB/FileXio path.

## BuildFix25 updater change

The TPUP payload path is now **forward-only**:

- no `TitanStorageIo_Seek64()` is used by `titan_system_update.c`;
- TPUP entries are required to be contiguous/in-order, matching `titan_pup.py` output;
- the verifier opens the package at byte 0, sequentially consumes header+table, then reads the payload straight through;
- extraction reopens the package, sequentially consumes header+table again, then extracts the payload in order;
- extraction alternates one read RPC and one write RPC per scheduler quantum, matching the proven Package Installer model;
- after extraction the PUP descriptor is closed and every staged file is reopened and CRC-verified before atomic publish/rollback.

The header+table maximum is only 2368 bytes, so sequentially consuming it has negligible cost and avoids depending on USB `lseek` behavior entirely.

## Versioning

- Installed updater generation: BuildFix25.
- Real PUP target: `Project-Titan-Magenta-Edition-0.3.90-buildfix25-stream-updater-flow-errors-wlaunchelf.pup`.
- Hardware self-test: Build26, output `Project-Titan-PUP-SelfTest-build26.pup`.

Because the running BuildFix23 updater is the component exhibiting the seek-dependent CRC mismatch, install the BuildFix25 `kernel.elf` manually once. After reboot confirms `/ build 25`, test the Build26 self-test PUP. If it succeeds, subsequent system updates can return to normal TPUP delivery.

## Validation

- system update transaction: PASS with forced 3-byte short I/O, corrupted payload rejection, staged reread, multi-file publish and rollback.
- BuildFix25 forward-only updater contract: PASS.
- BuildFix24 Flow/error/wLaunchELF feature contract: PASS.
- wLaunchELF native port contract: PASS.
- wLaunchELF host lifecycle: PASS (`draw-visible`).
