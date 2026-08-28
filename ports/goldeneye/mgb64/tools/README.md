# tools/

Build, extraction, validation, and diagnostic tooling for MGB64.

Two kinds of things live here:

1. **Maintained tool projects** — self-contained subdirectories, each with its
   own README and (where third-party) provenance. These are reusable and
   load-bearing.
2. **Loose top-level scripts** (`*.sh`, `*.py`) — verb-prefixed validation,
   regression, and diagnostic helpers. Some are wired into CI/CTest; many are
   single-purpose helpers written to investigate one specific issue. See the
   naming convention below before assuming a script is a general-purpose tool.

## Start here (the scripts you'll actually use)

| Script | What it does |
| --- | --- |
| `validate_quick.sh` | Fast pre-push lane: static source guard (no ROM needed) + a boot/spawn smoke if you have a build + ROM. Run this before opening a PR. |
| `asan_smoke.sh --gate` | AddressSanitizer boot/gameplay smoke; the sanitizer gate. |
| `perf_census.sh` | Per-level frame-time census across the campaign (writes `baselines/`). |
| `ctest_require_prereqs.sh` | CTest wrapper that reports honest `SKIPPED` (not failure) when a ROM/binary/oracle prerequisite is missing. |
| `*_smoke.sh` | Boot / mission-flow / progression smokes (e.g. `dam_progression_smoke.sh`). |

The full validation story — save/pixel/state/audio lanes, the trace schema, and
the diagnostic environment variables — is documented in
[`../docs/INSTRUMENTATION.md`](../docs/INSTRUMENTATION.md).

## Maintained tool projects (subdirectories)

| Directory | Purpose |
| --- | --- |
| `extractor/` | Pulls assets from **your** ROM at build time (no ROM data ships in-tree). |
| `asm-processor/` | MIPS assembly matching/preprocessing for decompilation work. |
| `ido5.3_recomp/` | Recompiled IDO 5.3 compiler used by the N64 ROM-matching build. |
| `gzipsrc/` | `gzip` source used for asset (de)compression in the pipeline. |
| `mktex/` | Texture conversion tool (N64 texel formats). |
| `texpack/` | Real-ESRGAN HD-texture upscaler pipeline (offline half of the HD-pack workflow). Fetches its large binary on demand — nothing ROM-derived is committed. |
| `smaa/` | Generates the SMAA area/search lookup tables for the post-FX path. |
| `metal/` | Native-Metal-backend spike/experiment helpers. |
| `report/` | Small C tool that renders an objectives/run HTML report. |
| `aaa_rip/` | Small standalone C helper. |
| `tests/` | Unit tests for the Python tooling here. |

Data (not code) directories:

| Directory | Contents |
| --- | --- |
| `campaign_routes/` | JSON input-route + objective-contract fixtures for automated campaign validation, one set per level. |
| `rom_oracle_routes/` | JSON oracle captures (camera paths, animation phase) used by the intro/outro faithfulness checks. |

Third-party components carry their own `PROVENANCE.md`/`LICENSE`; see
[`../THIRD_PARTY.md`](../THIRD_PARTY.md) for the inventory.

## Naming convention for loose scripts

| Prefix | Meaning | Wired to CI? |
| --- | --- | --- |
| `check_*_regression.{sh,py}` | Assertion gates — compare current behavior against a committed expectation. | Some are registered as CTest lanes (see `../CMakeLists.txt`). |
| `*_smoke.sh` | Boot / gameplay / progression smokes — "does the game reach this state." | Several, via `PORT_VALIDATION_TESTS`. |
| `analyze_* / compare_* / summarize_* / score_*` | Diagnostic and reporting helpers used while investigating an issue. | No — run by hand. |
| `audit_*` | One-shot source/asset audits. | No. |

> **Heads up:** a number of the `analyze_*/compare_*/glass_*` scripts were
> written to investigate one specific past issue (e.g. the glass-shard work) and
> are kept for reproducibility, not because they're general tools. If a script
> reads like a one-off, it probably is. When adding a new durable tool, prefer a
> subdirectory with a README over another loose top-level script.
