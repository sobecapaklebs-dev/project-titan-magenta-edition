# BuildFix27 Physical PS2 Evidence Format — CP48

Inputs are generated from the real console test; host output is not accepted as a substitute.

1. `debug-report.txt`: export **Debug Report** from the Titan DEX diagnostics screen after reaching HOME in Reference 720p. The validator consumes the existing `Video`, EE/IOP RAM, 720p path/output resolution, VRAM and `BOOT VRAM HISTORY` fields.
2. `hardware-events.jsonl`: one JSON object per completed physical action. Each object requires `event` and `success:true` for a passing action.

Required successful events:
- `{"event":"cold_boot_home","success":true}`
- lifecycle cycles 1 through 10: `{"event":"lifecycle_cycle","cycle":1,"success":true}` ... cycle 10
- `{"event":"network_online","success":true}`
- `{"event":"storage_online","success":true}`
- `{"event":"pfs_mount_rw","success":true,"owner":"TitanStorageMount","mount":"pfs0:/"}`
- `{"event":"ftp_roundtrip","success":true,"retr":true,"stor_atomic":true}`
- `{"event":"vmc_export","success":true,"atomic":true,"read_only_source":true}`

Validate with:
`python3 scripts/validate_buildfix27_hardware_evidence.py --debug-report debug-report.txt --events hardware-events.jsonl --output hardware-validation.json --require-pass`

A missing toolchain/PFS binary or unexecuted hardware step must remain absent/failed. Do not author a success event from host inference.
