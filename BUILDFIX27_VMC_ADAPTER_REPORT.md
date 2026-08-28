# BuildFix27 VMC Adapter Report — CP31

## Result
CP31 adds a bounded read-only filesystem inspection path for the active profile VMC without exposing a `vmc:` mount, FileXio ownership, or raw filesystem handles to native apps.

## Titan VMC service
- `TitanVmc_ListRoot()` parses the PS2 memory-card filesystem image already owned by Titan.
- It validates image size and superblock before traversal.
- Root directory traversal follows allocated FAT links with bounded steps and rejects malformed/free/self-referential links.
- `.` and `..` are hidden from the app view; entry names, size and mode-derived flags are normalized.

## Titan App ABI
- `TitanAppVmcEntry` and `vmc_list_active_root` are appended to ABI v1.
- Existing ABI offsets remain unchanged.
- Runtime binds the call to `TitanUserProfiles_GetActive()` and copies only normalized metadata into app memory.

## wLaunchELF
- R2 toggles an explicit active-profile VMC view.
- VMC entries are presented as read-only and cannot enter storage mutation paths.
- wLaunchELF still owns no FileXio, gsKit, ExecPS2, VMC mount, or raw image descriptor.

## Validation
- formatted/recovery/corrupt VMC host paths: PASS under ASan/UBSan;
- injected extended root FAT chain with directory + file entry: PASS;
- malformed unallocated FAT link rejection: PASS;
- Titan App runtime ET_REL/R5900 guard harness: PASS under ASan/UBSan;
- wLaunchELF host lifecycle including R2 VMC view: PASS;
- Project NES regression suite: PASS;
- lifecycle/classification, managed-storage and BF27 readiness gates: PASS.
