# Project Titan — BuildFix30 R4 Full Source Audit

## Release identity

- Candidate: `Project-Titan-BuildFix30-FINAL-FMCB-INTEGRATED-PREHARDWARE-R4-AUDITED`
- Baseline: `Project-Titan-BuildFix30-FINAL-FMCB-INTEGRATED-PREHARDWARE-R3`
- Baseline archive SHA-256: `46327cb293b5a1c36648a808ae69b30eb4b27f32a02c81b306e094b9d3d28e4f`
- Scope: whole-source pre-hardware audit, target-build dependency audit, host/sanitizer regression, static lifetime/bounds/error-path audit, installer/FMCB transaction audit, generated-artifact hygiene.
- Autonomous provider remains **Free McBoot only**. OpenTuna/Fortuna/FunTuna are not release fallbacks.
- Autonomous UI remains fail-closed: only safely identified compatible consoles expose Autonomous; incompatible/unknown consoles expose Semi-autonomous only.

## Validation state

- `GLOBAL_HOST_REGRESSION_PASS=1`
- `TARGET_SOURCE_AUDIT_PASS=1`
- `PYTHON_SYNTAX_PASS=1` (`443` Python files parsed)
- `SHELL_SYNTAX_PASS=1` (`174` shell files passed `bash -n`)
- `SOURCE_AUDIT_PASS=1`
- `TARGET_PREFLIGHT_SELFTEST_PASS=1` (`43` mandatory default-build IRXs modeled)
- `TREE_HYGIENE_PASS=1` (`0` in-tree `__pycache__`, `0` `.pyc` after final validation)
- `TARGET_BUILD_VALIDATED=0`
- `HARDWARE_VALIDATED=0`

`TARGET_BUILD_VALIDATED` and `HARDWARE_VALIDATED` remain zero because this executor does not contain the real PS2DEV/PS2SDK target toolchain or a physical PS2. R4 is therefore a **pre-hardware audited source candidate**, not a claim of physical FMCB/MagicGate/KELF cold-boot validation.

## Full regression execution

The canonical runner is `kernel/tests/run_host_tests.sh` (lines 1–1236 in this tree). The executor watchdog does not allow the complete runner to finish in one invocation, so the **unchanged runner was traversed in contiguous ranges** with the same `set -euo pipefail` semantics and the same project root. No test was removed or converted to a non-fatal check.

Coverage reached every command from line 1 through line 1236. The final BF30 range ended with:

- Theme v1/v2/v3/v4 lifecycle/back-compat: PASS
- Disc Service / Disc Engine / return / shutdown / no-frame-I/O: PASS
- Localization / EN-PT-ES / no-frame-I/O: PASS
- validated boot baseline / TINS integrity / corruption fail-closed: PASS
- PUP transaction / auto-reboot / shutdown quiesce: PASS
- public API freeze / repository protocol: PASS
- release regression meta-contract: PASS
- FMCB source / provenance / build wiring: PASS
- same-environment FMCB integration: PASS
- no secondary FMCB installer: PASS
- autonomous UI gate / ROM compatibility: PASS
- MC space / existing FMCB / backup / stage / write / verify / commit: PASS
- rollback / interrupted recovery: PASS
- FMCB → Titan entry → BuildFix6 bootloader chain: PASS
- autonomous integrity / recovery: PASS
- semi-autonomous non-regression: PASS
- negative OpenTuna/Fortuna path contract: PASS
- final target-source audit contract: PASS

The earlier ranges also revalidated storage/VMC, Neutrino/UL, Package Installer/TPKG, Titan Apps, renderer/pikaVR/VlombisCore, GoldenEye/TitanEYE host contracts, Theme Engine, Project NES/FCEUmm, network client/FTP, PFS provider lifecycle, Unified Input, DualSense/Xbox USB/Bluetooth/BLE/Early Input, keyboard/mouse, overlays/Smart IGR, Store/Download, Date/Time/NTP, Disc, localization and API freeze. Existing ASan/UBSan invocations in the runner remained enabled.

## R4 defects found and corrected

### 1. PS2SDK HID build dependency mismatch

`kernel/Makefile` referenced non-existent `usbkbd.irx` / `usbmouse.irx`. The audited PS2SDK target names are `ps2kbd.irx` and `ps2mouse.irx`.

Corrections:

- kernel inputs changed to `ps2kbd.irx` / `ps2mouse.irx`;
- diagnostics changed to the real filenames;
- BF28 keyboard/mouse contract updated to require the real names and reject stale aliases.

### 2. PADEMU USB/BT object wiring and IOP memory imports

BLE HOGP/crypto objects were incorrectly linked into the USB PADEMU target even though the Bluetooth backend consumes them. The IOP versions also emitted unresolved `memcpy`/`memset` references.

Corrections:

- `titan_ble_hogp.o` and `titan_ble_crypto.o` moved from USB target to BT target;
- IOP builds map memory operations to the existing SMSUTILS `mips_memcpy`/`mips_memset` imports;
- Xbox USB/BT/BLE contracts revalidated.

### 3. DS34USB descriptor/lifetime bugs

Both PADEMU and standalone UL-engine DS34USB implementations contained target-only hazards:

- device descriptor could be dereferenced before assignment;
- OUT endpoint state could begin as `0` rather than the invalid sentinel;
- arbitrary descriptor walking could pass the configuration blob;
- an error path called cleanup while already holding the same semaphore;
- control/interrupt/OUT endpoints were not consistently closed on partial connection failure.

Corrections:

- descriptor pointers initialized and validated before publication;
- endpoint sentinels initialized to `-1`;
- descriptor walks bounded by `wTotalLength`, per-descriptor length and interface boundary;
- recursive semaphore cleanup removed;
- partial endpoint acquisition is unwound explicitly.

### 4. DS34BT descriptor and endpoint cleanup

The PADEMU and standalone BT copies received equivalent bounded descriptor parsing and endpoint cleanup. Disconnect now verifies the active device identity before tearing down the Bluetooth transport. Control and charge-controller endpoint leaks were closed.

### 5. AR9271 resident-module startup transaction

The AR9271 `_start()` path could return `MODULE_NO_RESIDENT_END` after creating threads/semaphores or registering NetMan/USBD. That creates a potential execution-after-module-unload condition.

Corrections:

- startup tracks created threads, semaphores, NetMan and USBD registration;
- all `StartThread()` results are checked;
- failure unwinds threads/semaphores and unregisters NetMan/USBD;
- required `DeleteThread`, `TerminateThread`, `DeleteSema` and USBD unregister imports added;
- RPC startup is deliberately the final fallible startup step;
- whole-translation-unit AR9271 syntax guard expanded.

### 6. AR9271 flight recorder synchronization

Flight recorder lock creation could fail while the driver continued and later used the recorder concurrently without a valid semaphore.

Corrections:

- flight recorder exposes `titan_fr_lock_ready()`;
- `_start()` calls `titan_fr_reset()` and fails non-resident before creating worker threads if the recorder lock is unavailable;
- final static contract enforces ordering: recorder reset → lock proof → thread creation.

### 7. Titan Asset Service worker race

The Asset Service could fail its mutex allocation, mark itself faulted, and later clear the fault while starting the network worker without restoring synchronization.

Corrections:

- lock creation moved behind recoverable `TitanAssetService_EnsureLock()`;
- the worker cannot clear the fault/start until lock recovery succeeds;
- Asset Service contract and EE syntax guard pass.

### 8. Native app HTTP worker thread leak

A completed native-app network worker cleared its own thread ID and called `ExitThread()`, leaving no controller capable of calling `DeleteThread()`.

Correction:

- the completed worker now uses `ExitDeleteThread()`;
- `StartThread()` failure still explicitly deletes the created thread object;
- the separate App Runtime init-thread path was not mechanically changed because it retains external ownership and an explicit delete path.

### 9. HTTP streaming undefined behavior and counter overflow

Streaming mode legitimately permits a NULL destination because chunks are delivered through `body_write`. Chunked parsing nevertheless formed `destination + used`, which is undefined behavior even if the downstream function does not dereference it. Accumulated byte counters also lacked complete overflow protection.

Corrections:

- chunked destination arithmetic is conditional on a non-NULL destination;
- accumulated chunk size rejects `UINT_MAX` overflow;
- `Content-Length` values wider than runtime counters fail closed;
- unframed streaming checks both body-size and result telemetry counters for wrap;
- final static contract protects these invariants.

### 10. Boot-video backing allocation lifetime

`TitanBootVideo_Show()` could retain a previous `memalign()` backing allocation across retry and leak the new allocation if initial upload or fade-in failed.

Corrections:

- stale composite backing is released before allocation;
- upload failure releases and nulls backing;
- fade failure releases and nulls backing and clears readiness;
- final static contract guards retry/upload/fade cleanup.

### 11. FMCB integrated IOP bootstrap hardening

The integrated backend inherited indefinite IOP waits and several ignored module failures.

Corrections:

- IOP reset/sync waits are bounded and return timeout;
- IOMANX/FILEXIO, USB, SECRSIF/MagicGate and MCTOOLS startup failures fail closed;
- `fileXioInit()` is checked;
- physical `rom0:ROMVER` availability/length is checked before upstream regional-path code;
- FMCB root probe uses bounded `snprintf` result validation;
- semaphore creation errors clear callback state before returning.

### 12. Upstream-integrated FMCB semaphore error propagation

Eight upstream call sites assumed `IopInitStart()` always returned a valid semaphore and could call `WaitSema()` on a negative ID.

Correction:

- each integrated call site propagates a negative initialization result before any wait/delete operation.

### 13. Autonomous rollback after FMCB IOP replacement

On an integrated FMCB failure, Titan could start restoring staged files while its storage/RPC clients still belonged to the IOP generation replaced by the FMCB backend.

Correction:

- Titan restores its own IOP/storage environment before starting filesystem rollback;
- restore failure is explicitly logged as `FMCB_ROLLBACK_IOP_RESTORE` and the transaction remains fail-closed.

### 14. Target format truncation warning

The target compiler exposed a potential `snprintf` truncation in recovery notification composition.

Correction:

- detail is explicitly bounded to the available message budget.

### 15. Full target dependency preflight

The release umbrella previously discovered missing target dependencies only after host regression or partial compilation.

New `tools/titan_target_preflight.py` is invoked before the expensive host suite and checks the default build graph for:

- EE gcc/ar/strip;
- IOP gcc/ar/strip;
- DVP assembler/objcopy;
- `bin2c`, `iopfixup`, `ps2-packer`;
- core PS2SDK make fragments;
- 43 mandatory IRXs used by bootloader/kernel/Neutrino/UL default paths;
- libpng/libjpeg/zlib/FreeType/Vorbis/Ogg ps2sdk-ports inputs;
- gsKit + dmaKit;
- integrated FMCB release inputs;
- non-empty `TITAN_AR9271_FW`.

Optional MMCE and debug-only backends remain Makefile-gated rather than becoming false release blockers.

A synthetic complete toolchain/dependency tree exercised the validator itself and produced `TARGET_PREFLIGHT_PASS=1`, `TARGET_PREFLIGHT_IRX_COUNT=43`.

## Static-contract additions

`kernel/tests/test_buildfix30_target_source_audit_contract.py` now guards:

- PS2SDK HID names and absence of stale aliases;
- PADEMU USB/BT BLE wiring and IOP memory imports;
- USB/BT descriptor bounds and endpoint cleanup;
- AR9271 transactional startup/import requirements;
- Asset Service lock-before-worker invariant;
- release-build preflight coverage;
- bounded/fail-closed FMCB IOP bootstrap;
- upstream FMCB semaphore error propagation;
- Titan-I/O restoration before rollback;
- target message formatting bound;
- flight recorder synchronization before AR9271 workers;
- NULL-safe HTTP chunked streaming and overflow rejection;
- native-app network worker self-deletion;
- boot-video backing cleanup on retry/upload/fade failure.

## Final hygiene pass

The audit tree was compared byte-for-byte with a fresh extraction of the R3 archive. Test execution had created 305 non-baseline artifacts, including host binaries, regenerated Project NES provider package output, `__pycache__` and `.pyc` files. All were removed before packaging.

Final post-cleanup checks:

- Python syntax parse: `443/443` PASS;
- shell `bash -n`: `174/174` PASS;
- `test_buildfix30_target_source_audit_contract.py`: PASS;
- AR9271 full translation-unit syntax guard: PASS;
- Titan Asset Service EE syntax guard: PASS;
- Titan native HTTPS/image-decoder syntax guard: PASS;
- `kernel/tools/titan_source_audit.py`: PASS;
- allowed binary payload provenance audit: PASS (`14` allowlisted payloads, `31` license/notice files);
- in-tree `__pycache__`: `0`;
- in-tree `.pyc`: `0`.

## Source delta before release metadata

Relative to pristine R3, the audited implementation contains:

- 24 modified source/build/test files;
- 2 new implementation/audit files (`tools/titan_target_preflight.py` and `kernel/tests/test_buildfix30_target_source_audit_contract.py`);
- 0 deleted R3 files.

Release metadata added by R4 itself (this report, R4 status and R4 manifest) is in addition to that implementation delta.

## Remaining external gates

The following cannot be truthfully closed by this executor:

1. build the complete R4 target with the user's pinned PS2DEV/PS2SDK/gsKit/ports environment and validated AR9271 firmware;
2. confirm the umbrella build completes with no new R5900/IOP linker/fixup errors;
3. build installer media and verify no standalone `FMCBInstaller.elf` is emitted;
4. perform clean-Memory-Card autonomous install on a known compatible ROM <= 2.20 console;
5. verify MagicGate/KELF signing and cold power-on FMCB → Titan entry → frozen BuildFix6 bootloader → kernel/system;
6. test interrupted install/rollback/recovery and semi-autonomous non-regression on physical hardware.

Until those are executed, R4 is correctly classified **PREHARDWARE AUDITED**.
