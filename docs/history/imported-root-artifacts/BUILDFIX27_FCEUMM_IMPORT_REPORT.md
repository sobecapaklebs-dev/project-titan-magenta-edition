# BuildFix27 CP42 — Frozen FCEUmm Source Import Pipeline

STATUS: IMPORT_PIPELINE_HOST_VALIDATED / SOURCE_BYTES_BLOCKED

Frozen identities remain unchanged:
- modern: libretro/libretro-fceumm `b5e3566515c27dc66c9c20572171673126532e06`
- PS2 reference: ps2homebrew/Fceumm-PS2 `dc77e3ca4baad08f0023f61dba9846550735b74d`

The current executor still cannot materialize GitHub codeload archives: direct container DNS resolution fails, and the archive redirect cannot be delivered into the build filesystem. This is an environment/material transport blocker, not permission to substitute another revision.

`scripts/import_fceumm_sources.py` now supplies a deterministic ingestion boundary. It:
- accepts only the two frozen profiles;
- requires the archive's sole top-level root to identify the frozen commit;
- rejects absolute paths, `..`, symlinks, hardlinks and device entries;
- computes canonical Git blob SHA-1 (`blob <len>\0<data>`) for critical source/license files and matches fingerprints obtained from the frozen GitHub refs;
- verifies recognizable GPL-2.0 license material;
- extracts only after full verification and publishes the destination atomically.

Critical fingerprints currently pinned include modern `Copying`, `Makefile.common`, `src/x6502.c`, `src/ppu.c` and PS2 `Copying`, `src/x6502.c`, `src/cart.c`.

No FCEUmm source code is claimed imported or linked by CP42.
