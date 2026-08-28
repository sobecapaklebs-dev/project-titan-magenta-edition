# BuildFix27 Checkpoint CP43
STATUS: HOST_VALIDATED / MATURE_PROVIDER_OPTIONAL
PREVIOUS_VALID: CP42
BLOCK: Project NES mature provider selection + GPL packaging gate

- Added versioned ProjectNesProviderDescriptor with provider-owned context.
- FCEUmm provider resolution is weak/optional; absent provider keeps Legacy NROM fallback.
- Candidate provider is accepted only with complete backend callbacks, GPL-2.0 SPDX identity and one of the two frozen FCEUmm commits.
- Invalid license, commit, ABI or callback set fails closed to Legacy.
- Packaging gate rejects FCEUmm distributions without GPL-2.0 combined-module labeling, upstream license text and frozen-source provenance.
- Default package remains Legacy NROM/MIT because FCEUmm source bytes are still not materialized or linked.

NEXT: CP44 final BF27 host closure/material-chain audit + target handoff refresh.
