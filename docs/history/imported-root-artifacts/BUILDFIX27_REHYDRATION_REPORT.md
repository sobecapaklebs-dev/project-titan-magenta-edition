# BuildFix27 CP49 — Deterministic Lineage Rehydration

STATUS: HOST_VALIDATED

BuildFix27 CP48 can now be reconstructed without trusting a mutable development workspace.

Material chain:
- full source base: Project-Titan-0.3.92-rc16-TitanEYE-Ribbon-BuildFix27-CP14-source.tar.gz
- fixed base SHA-256: f1bc0c83e46661ac672ba6e7225d78053a496a819381543f865937dea824caab
- incremental chain: CP15 through CP48
- every delta archive SHA-256 is locked inside scripts/rehydrate_buildfix27.py and cross-checked against its persisted SHA256SUMS file
- embedded manifest/checkpoint parentage is checked whenever present; historical deltas without embedded parent metadata remain bound by the locked sequential lineage and immutable archive SHA
- tar extraction rejects absolute paths, parent traversal, links and device entries
- final tree must declare LATEST_VALID_CHECKPOINT: CP48

Validation performed from scratch into /tmp:
- CP14 + CP15..CP48 reconstruction: PASS
- final progress checkpoint: CP48
- source equivalence against current CP48 workspace: 104 files checked, 0 missing, 0 mismatches
- official BF27 closure segment through FCEUmm importer: PASS before executor time limit
- remaining CP45/CP46/CP47/CP48 gates executed separately: PASS

This tool does not fabricate target validation. The reconstructed CP48 tree still truthfully reports the external FCEUmm, PS2DEV/PFS binary, PCSX2 and physical-PS2 gates as open.
