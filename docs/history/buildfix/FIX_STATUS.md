# Full audit correction status

This matrix covers the 49 issue groups recorded against the consolidated RC1
snapshot. “Source resolved” means the maintained source and host contracts now
contain the correction. It does **not** mean the corrected MIPS binary has been
validated on a physical PS2.

## P0 — release blockers

| ID | Finding | RC3 source status |
|---|---|---|
| P0-01 | GS state leak after VU1 PATH1 | Source resolved: canonical texture, clamp and blend state restored before PATH3 UI. |
| P0-02 | CPU point-by-point main icons | Source resolved: 11 icons use the uploaded UI texture atlas. |
| P0-03 | 720p single-buffer presentation/reconfigure safety | Source resolved in RC3: two page-aligned native buffers, VSync-only page flip, full VRAM rebuild/reupload and rollback. Hardware timing pending. |
| P0-04 | Blocking/incorrect audio runtime | Source resolved: bounded ring streaming, exact WAV validation, partial-enqueue tail retention, retry/shutdown and memory limits. |
| P0-05 | Destructive profile save | Source resolved: verified `.tmp`/`.bak` publication and recovery. |
| P0-06 | Unsafe library cache and partial scan publication | Source resolved: explicit schema/checksum/validation and publish-only-complete scan. |
| P0-07 | Direct unvalidated application ELF launch | Source resolved: shared strict validation plus stage2 handoff. |
| P0-08 | Input owned/gated unrelated IOP services | Source resolved: dedicated IOP service manager and independent clients. |
| P0-09 | Divergent weak ELF validators | Source resolved: one common validator with bounds, alignment, overlap and executable-entry checks. |
| P0-10 | Neutrino FHI overlap/partial commit | Source resolved: maximum occupied end, staged validation and rollback. |
| P0-11 | UL active-profile VMC parity | Source resolved: explicit `-vmc0=` handoff and VMC image validation in Titan UL Engine. Hardware game verification pending. |
| P0-12 | Installer wrote a stale secondary kernel path | Source resolved: canonical root kernel only; legacy path disabled. |
| P0-13 | Host test absolute path | Source resolved: tests derive paths from the checked-out tree. |
| P0-14 | Narrow/partial storage I/O | Source resolved: exact wide helpers with overflow and zero-progress handling. |
| P0-15 | Asset provenance/distribution ambiguity | Source resolved: reference-derived UI artwork replaced by deterministic first-party assets; Inter license and SFX hashes documented; optional music excluded. |

## P1 — high-priority correctness and product quality

| ID | Finding | RC3 source status |
|---|---|---|
| P1-01 | 720p nonuniform scaling | Source resolved: uniform scale and centered logical canvas. |
| P1-02 | PSM16 banding/dither policy | Source resolved as an explicit conservative policy; global DTHE remains off until physical A/B testing. |
| P1-03 | VRAM alignment/overflow/name lifetime | Source resolved with checked arithmetic and owned allocation labels. |
| P1-04 | Borrowed VRAM allocation names | Source resolved. |
| P1-05 | Misleading DEX/FTP labels | Source resolved: visible activity/diagnostic wording states what is actually implemented. |
| P1-06 | Controller reconnect edge events | Source resolved: neutral first stable frame. |
| P1-07 | Incomplete input/IOP unwind and retry | Source resolved through explicit lifecycle and bounded retry. |
| P1-08 | Superficial VMC validation | Source resolved: structural superblock/IFC/FAT/root checks. |
| P1-09 | VMC format written twice | Source resolved: one-pass image creation and verified publication. |
| P1-10 | Incomplete VMC recovery | Source resolved: `.new`/`.bak` recovery before READY. |
| P1-11 | ISO extent arithmetic bounds | Source resolved with wide overflow and image-length checks. |
| P1-12 | First malformed `BOOT2` stopped parsing | Source resolved: parser continues to later valid assignments. |
| P1-13 | Scanner overclaimed local ZSO/CSO/CHD | Source resolved: unsupported local forms are not advertised; compatibility matrix is explicit. |
| P1-14 | Weak catalog record validation | Source resolved with enum/path/string/record bounds. |
| P1-15 | Frame-count-dependent timing | Source resolved with monotonic milliseconds and fixed-step accumulation. |
| P1-16 | Silent notification drops | Source resolved with coalescing, priority replacement and drop telemetry. |
| P1-17 | Invalid UTF-8 handling | Source resolved with strict sanitizer and U+FFFD progress. |
| P1-18 | Silent visual-asset initialization failure | Source resolved with explicit states, results and retry. |
| P1-19 | Neutrino UL loops/arithmetic | Source resolved with bounded delayed retries, exact reads and 64-bit totals. |
| P1-20 | OPL resource cleanup | Source resolved on Titan direct-launch paths with explicit close/free/error exits. |
| P1-21 | OPL build mutated the maintained/user tree | Source resolved: disposable temporary build copy. |
| P1-22 | Non-atomic DEX configuration | Source resolved using verified transactional storage. |
| P1-23 | UI placeholder services presented as real | Source resolved through truthful labels/status text; no fake service claim. |

## P2 — maintainability and release engineering

| ID | Finding | RC3 source status |
|---|---|---|
| P2-01 | Shell scene monolith | Materially reduced by extracting audio, time, UTF-8, notifications, cache and lifecycle services. Further aesthetic decomposition is allowed but no longer blocks subsystem ownership/testing. |
| P2-02 | Other oversized service modules | Materially reduced through shared storage, ELF, cache and IOP modules. Upstream engine monoliths remain attributed upstream code. |
| P2-03 | Backup attempts in active tree | Source resolved: removed and contract-tested. |
| P2-04 | Generated binaries/headers in source package | Source resolved: generated production headers and local runtime binaries are untracked; `git archive` is the package contract. |
| P2-05 | Compiler warning policy | Host-owned code uses `-Wall -Wextra -Werror`; MIPS warning closure is a build gate, not yet executable here. |
| P2-06 | Duplicated paths/strings | Reduced to documented canonical path tables and shared helpers. Legacy aliases exist only for controlled migration. |
| P2-07 | Partial launch logging | Source resolved on stage2, Titan UL direct launch and installer manifests. |
| P2-08 | Missing toolchain manifest | Source resolved: `TOOLCHAIN.lock` and build manifest. |
| P2-09 | No reproducible build path | Source resolved: two-clean-tree hash comparison mode. |
| P2-10 | No fault-injection matrix | Source resolved: `docs/FAULT_INJECTION_MATRIX.md` plus host tests. |
| P2-11 | Missing architecture/contracts documentation | Source resolved under `docs/`. |

## Release gate

The known source-audit blockers are addressed. The remaining gate is empirical:
clean MIPS compilation, reproducibility comparison and physical PS2 tests for
480i/720p, audio, storage interruption recovery, profile/VMC use and a per-game
Titan Neutrino/Titan UL compatibility matrix.
