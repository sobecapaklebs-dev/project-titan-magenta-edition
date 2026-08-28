# BuildFix27 Checkpoint CP49
STATUS: HOST_VALIDATED / EXTERNAL_TARGET_BLOCKERS_UNCHANGED
PREVIOUS_VALID: CP48
BLOCK: deterministic material-lineage rehydration

Implemented:
- safe CP14 full-source + CP15..CP48 delta rehydrator;
- immutable SHA-256 lock for every archive in the reconstruction chain;
- SHA256SUMS cross-check and embedded parentage conflict detection;
- safe tar extraction and atomic output publication;
- final CP48 identity gate;
- permanent host contract and target handoff documentation.

Validation:
- from-scratch rehydration: PASS;
- 104 source/script/doc files: byte-identical, 0 missing, 0 mismatches;
- BF27 closure through importer: PASS before executor timeout;
- runner tail CP45-CP48: PASS.

External blockers remain FCEUmm bytes, PS2DEV/IOP/VU toolchain + PFS IRX binaries, PCSX2 and physical PS2 execution.
