# BuildFix27 Checkpoint CP42
STATUS: HOST_VALIDATED / SOURCE_MATERIAL_BLOCKED
PREVIOUS_VALID: CP41
BLOCK: deterministic exact FCEUmm source import pipeline

- Frozen modern and PS2 commit pins unchanged.
- Added safe tar.gz verifier/importer with commit-root, Git blob and GPL-2.0 checks.
- Path traversal, links/devices and wrong-source archives are rejected before extraction.
- Extraction/publish is atomic after full verification.
- Host fixtures prove Git blob hashing, traversal rejection and wrong-snapshot rejection.
- Current environment still cannot deliver codeload archive bytes; no FCEUmm code is claimed imported/linked.

NEXT: CP43 optional mature-provider selection + packaging/license gate while preserving Legacy NROM fallback.
