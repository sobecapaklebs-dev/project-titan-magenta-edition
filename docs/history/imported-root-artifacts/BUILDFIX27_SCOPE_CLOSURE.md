# BuildFix27 Host Scope Closure — CP44

STATUS: HOST_SCOPE_CLOSED_WITH_EXTERNAL_BLOCKERS

Baseline: BuildFix26 FINAL remains immutable. BuildFix27 lineage is CP10R recovery -> CP33 -> CP34.

## Original scope disposition

### wLaunchELF native integration
- DONE_HOST: bounded recursive copy/move with two-phase move cleanup.
- DONE_HOST: rename/new-folder via Titan text input + Storage/VFS.
- DONE_HOST: HDD capability truthfulness; hdd0 remains read-only.
- DONE_HOST: TitanStorageMount provider-gated PFS/HDD mount lifecycle, slot generations, capability/state reporting and rollback are implemented and tested.
- DONE_HOST: concrete PS2 FileXio PFS platform hooks are implemented and fail closed until both `hdd` and `pfs` IOMANX devices are registered.
- DONE_HOST: optional Titan-owned IOP dependency loader is implemented with resident-module detection and order `dev9 -> atad -> hdd_driver -> pfs_driver`; Network Core reuses resident DEV9.
- BLOCKED_EXTERNAL_BINARY: official target drivers `ps2atad.irx`, `ps2hdd.irx` (APA/HDD) and `ps2fs.irx` (PFS) are not materialized in this workspace; therefore no writable PFS root is advertised on the current target state.
- DONE_HOST: Titan-owned active-profile VMC inspection, nested navigation, bounded file reads and transactional export without exposing a vmc: mount.
- DONE_HOST: Titan-rendered bounded text viewer/editor and JPEG/PNG FIT viewer.
- DONE_HOST: FTP control, PASV/EPSV LIST/NLST, RETR and transactional STOR behind Titan Network Stream; app sees only service controls.
- DONE_HOST: validated Titan stage2 external ELF handoff remains preserved.

### Project NES core replacement
- DONE_HOST: frontend/core boundary, Titan video/input/audio/storage adapters, durable SRAM lifecycle, ROM-free mapper/region/audio conformance, 10-cycle cleanup/fault tests.
- DONE_HOST: deterministic frozen-source importer verifies commit-root identity, critical Git blob fingerprints, GPL-2.0 material and safe extraction.
- DONE_HOST: versioned mature-provider descriptor selects only complete GPL-2.0 FCEUmm providers from the frozen commits; provider-owned context is supported and invalid/missing providers fail closed to Legacy NROM.
- DONE_HOST: packaging gate rejects FCEUmm combined modules that omit GPL-2.0 labeling, upstream license text or frozen-source provenance.
- BLOCKED_EXTERNAL_MATERIALIZATION: selected FCEUmm provider is not linked because exact source archive bytes still cannot be delivered into the build filesystem in this environment. Exact frozen commits remain libretro-fceumm b5e3566515c27dc66c9c20572171673126532e06 and Fceumm-PS2 dc77e3ca4baad08f0023f61dba9846550735b74d. Legacy NROM fallback remains truthful.
- BLOCKED_EXTERNAL_TOOLCHAIN: unchanged PS2 FCEUmm/R5900 viability build and target performance measurements require PS2DEV/R5900 toolchain.

### Browser + Titan Network Core
- DONE_HOST: Browser no longer owns direct PS2IP. DNS/TCP/HTTP/HTTPS are behind Titan-owned network services.
- DONE_HOST: deadlines, cancellation, single-flight arbitration, opaque TCP stream handles and storage download ownership are contracted and host-tested.
- DONE_HOST: TLS remains capability-based through the existing bounded HTTP client; no unsupported HTTPS claim added.

### Cross-cutting contracts / SDK
- DONE_HOST: lifecycle/reclaim cycles, SAS/runtime independence, SDK ABI append-only checks, ownership documentation, wLaunchELF ownership constraints, Project NES ROM-free fixtures, Browser no-direct-PS2IP, network timeout/cancel and transactional storage paths.

## External release gates still open
1. PS2DEV/R5900/VU toolchain build.
2. PCSX2 target execution.
3. Physical PS2 cold boot / 720p watermarks / Network+Storage / repeated lifecycle validation.
4. Materialize and integrate exact FCEUmm source snapshots, then perform GPL-2.0 packaging/source-obligation review and target measurements.
5. Materialize/build official PS2SDK `ps2atad.irx` + `ps2hdd.irx` + `ps2fs.irx`; the CP41 Titan-owned lazy loader then supplies the dependency chain before target PFS validation.

No external blocker is reclassified as complete by CP34.

## CP36 PFS application boundary
- DONE_HOST: append-only provider-gated PFS availability/mount/status/unmount ABI.
- DONE_HOST: per-session mount ownership and deterministic reclaim on Titan App teardown.
- DONE_HOST: dynamic pfsN:/ allowlist and volume enumeration only for ONLINE capability-backed mounts.
- DONE_HOST: FileXio provider hooks + device-presence gate are implemented.
- BLOCKED_EXTERNAL_BINARY: `ps2hdd.irx` + `ps2fs.irx` are absent/unbuilt here; no target mount is claimed.

## CP45 target-environment intake
- DONE_HOST: deterministic external PS2DEV bundle validator verifies EE/IOP/VU tools, PS2SDK source units and ps2atad.irx/ps2hdd.irx/ps2fs.irx before emitting env.sh.
- TARGET remains blocked until a complete external bundle is materially present.

## CP46 FCEUmm adapter
- DONE_HOST: link-gated libretro adapter with provider-owned context, RGB565 presentation, 1024-frame frontend audio capacity, save RAM bridge and deterministic core shutdown.
- BLOCKED_EXTERNAL_MATERIALIZATION remains: exact frozen FCEUmm source bytes are still required before real target link.

## CP47 target execution boundary
- DONE_HOST: deterministic target build/evidence orchestrator consumes only a CP45-valid PS2DEV bundle, builds kernel+wLaunchELF+Project NES with the real EE compiler, validates PFS prerequisites and records artifact SHA-256/size.
- DONE_HOST: optional PCSX2 execution is caller-explicit and captured; absence is NOT_EXECUTED, never PASS.
- BLOCKED_EXTERNAL_TOOLCHAIN/PCSX2: this executor still has no material target bundle or emulator executable.

## CP48 physical evidence boundary
- DONE_HOST: fail-closed validator consumes the Titan-exported Debug Report plus an explicit physical event ledger.
- DONE_HOST: release evidence requires Reference 720p / 1280x720, valid EE+IOP watermarks, zero EE allocation failures, no CRITICAL boot VRAM pressure, HOME boot history, ten successful lifecycle cycles, Network+Storage, Titan-owned PFS RW, FTP RETR/atomic STOR and read-only-source/atomic VMC export.
- BLOCKED_EXTERNAL_HARDWARE: no physical PS2 run is fabricated by this executor.

## CP49 lineage rehydration
- DONE_HOST: deterministic CP48 reconstruction from full CP14 source + SHA-locked CP15..CP48 deltas.
- DONE_HOST: 104 source/script/document files compared byte-for-byte against the CP48 workspace with zero mismatch.
- External target blockers remain unchanged.
