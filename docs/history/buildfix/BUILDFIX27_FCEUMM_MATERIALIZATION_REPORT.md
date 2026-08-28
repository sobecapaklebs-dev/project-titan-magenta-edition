# BuildFix27 FCEUmm Materialization Gate — CP30

Status: **BLOCKED_EXTERNAL_MATERIALIZATION / PROVENANCE_VERIFIED**.

## Frozen provenance re-verified
- Modern functional reference: `libretro/libretro-fceumm` commit `b5e3566515c27dc66c9c20572171673126532e06`.
- PS2/R5900 reference: `ps2homebrew/Fceumm-PS2` commit `dc77e3ca4baad08f0023f61dba9846550735b74d`.
- GPL-2.0 distribution/source obligations remain mandatory for any linked provider.

The GitHub connector resolved both exact commits in CP30. The modern commit's source diff still contains its explicit `platform=ps2` R5900 target configuration; the PS2 fork commit resolves to the save-persistence fix previously frozen by CP07.

## Materialization result
A byte archive could not be placed in the build filesystem. The container's direct Git transport still fails DNS resolution for `github.com`, and the connector provides repository/commit/file reads but not a mountable full repository archive to the container. Therefore CP30 does **not** claim source import, provider linking, mapper expansion or replacement of the Legacy NROM fallback.

This is a materialization boundary, not a design uncertainty: exact commit identities and license are known, but source bytes needed by the build are absent.
