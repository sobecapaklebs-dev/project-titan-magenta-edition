# Contributing to Project Titan

Project Titan targets a real PlayStation 2. Compatibility, bounded resource use and fail-safe boot behavior take priority over feature count.

## Pick the API level that matches your work

### High — app/game author
Use the Titan Application SDK. You get lifecycle, files, HTTP/network, audio, graphics and normalized input without needing to understand GS registers, DMA or VRAM allocation. Prefer this level for new apps and games.

### Mid — runtime/service author
Use documented Titan Runtime, TGR, VFS, Session, Network and capability contracts. Preserve ownership rules, cancellation, fixed/bounded queues and zero/low-cost disabled paths.

### Advanced — PS2/platform work
Use TGR render-state/format/sampler hints and the documented platform contracts. Direct GS/DMA/VU work is acceptable only where the relevant ownership/lifetime contract is understood. pikaVR/VlombisCore internals are not a stable public ABI merely because their source exists.

## Before submitting changes

Run the relevant host contracts and, for release-oriented changes:

```sh
python3 kernel/tools/titan_source_audit.py .
bash kernel/tests/run_host_tests.sh
```

Do not add commercial ROMs/disc images, keys, credentials, private assets or third-party code without license/provenance. Do not turn a hardware-pending result into a fake PASS.

## Public-source release hygiene

For changes intended for the public Project Titan source tree:
- Do not commit commercial or copyrighted ROMs, ISOs, BIOS data, or proprietary game assets.

The repository release audits remain authoritative and fail closed for unapproved payloads and missing provenance.
