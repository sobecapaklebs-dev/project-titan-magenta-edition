# BuildFix28 Validation

Date: 2026-08-20

Result: **SOURCE IMPLEMENTATION COMPLETE / GLOBAL HOST REGRESSION PASS / TARGET + HARDWARE VALIDATION PENDING**

## Baseline
BuildFix27 final source freeze was verified before BF28 development: 4272 expected/actual files, zero missing/unexpected/hash mismatches, real frozen FCEUmm integrated.

## BF28 material host coverage
PASS includes:
- Titan Session Service and deterministic lifecycle;
- Native/Application Runtime + Project NES Overlay/Resume/Return;
- Titan UL Overlay/Resume/Return;
- Titan-Neutrino Overlay/Resume/Return;
- adaptive IGR compatibility and fail-closed UNKNOWN policy;
- Unified Input, DualSense USB/BT protocol paths, Xbox One/Series USB GIP and Bluetooth/BLE backend, Early Input, keyboard/mouse;
- platform overhead profiler, fast paths and no-frame-allocation contracts;
- pikaVR and VlombisCore pre-freeze contracts;
- Theme API v1/v2/v3 regression;
- Theme v4 schema/toolchain/runtime/renderer, parallax, independent timelines, transforms, HOME/TOP synchronization, rare events, budgets/degradation, DEX profiler and 14-layer synthetic flagship workload;
- boot/storage/audio/network/VMC/SAS/native apps/Project NES/FCEUmm/wLaunchELF/updater/renderer/720p/recovery historical regressions.

Sanitizer-bearing harnesses retained ASan+UBSan. The global runner completed through its final BF28 lifecycle/recovery gate. Timeouts during execution were resumed at exact script boundaries and never counted as PASS.

## Regressions corrected during final host run
1. Neutrino historical library-scan harness now links the Session Service + Time dependencies introduced by BF28.
2. Early Input source/kernel API mismatch was closed with a single `TitanEarlyInput_Pump(uint64_t now_ms)` API.
3. Early Input IRX loading is asynchronous/fail-open via fixed-stack EE worker; a 10-second startup deadline disables the modern backend without blocking Health & Safety or DS2.
4. Two BF27 closure contracts were updated to accept the final frozen BF27 status vocabulary (`EXTERNAL_VALIDATION_PENDING_PS2DEV`, `FCEUMM_REAL_INTEGRATED=1`) without altering BF27 artifacts.
5. CP17 rehydration audit materialized the lifecycle source that CP15/CP16 validation had exercised but their incremental archives had omitted: Session shutdown, Theme/background asset shutdown, shell teardown ordering, and final-runner `SCRIPT_DIR` initialization.

## Rehydration closure
The persisted BF27 freeze plus BF28 checkpoint archives through CP17 were treated as the source of truth. The official host runner passed from that rehydrated tree in exact continuation segments, ending at the final BF28 lifecycle/recovery gate. `REHYDRATABLE_SOURCE_GATE=PASS`.

## External validation state
- PS2DEV/PS2SDK: not present in this executor.
- `mips64r5900el-ps2-elf-*`: not present.
- PCSX2: not present.
- physical PS2/controllers: not available.

Therefore target build, PCSX2 and hardware validation remain external. These are not represented as PASS.
