# BuildFix27 Checkpoint CP38
STATUS: HOST_VALIDATED / CONCRETE_TARGET_HOOKS_BLOCKED
PREVIOUS_VALID: CP37
BLOCK: target PFS provider adapter + boot lifecycle integration

- Kernel boot now initializes TitanStorageMount before storage scheduler/service use.
- Added TitanStoragePfsProviderPs2 weak-hook adapter; it registers only when all platform hooks exist and report available.
- Builds without a concrete PFS backend remain linkable and fail closed.
- Host fixtures prove both absent-backend and linked-backend registration/mount/unmount behavior under ASan/UBSan.
- Adapter does not guess or hardcode fileXioMount/fileXioUmount signatures.
- CP35 lifecycle, CP36 app ABI and CP37 wLaunchELF UX remain green.

NEXT: CP39 concrete PS2 PFS platform hooks / provenance gate.
