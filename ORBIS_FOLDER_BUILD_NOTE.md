# OrbisOS folder pass — build note

This source package contains the Project Titan Magenta Edition Home-folder implementation added after the 0.3.90-rc2 baseline.

Host validation completed successfully with `scripts/validate_source.sh`, including the new folder persistence tests. The package intentionally omits the stale pre-change kernel ELF and the changed kernel object files so the PS2 toolchain rebuilds them from source.

A PS2/MIPS build was not produced in the validation container because PS2DEV/PS2SDK is not installed there. Build the kernel with the project's normal PS2DEV environment before hardware/emulator testing.


## System-tile contextual-options pass

`SELECT` now also opens contextual options for Home system tiles and the top system menu. Tile order/visibility is persisted transactionally in `SHELLLAY.DAT`; export/import uses `SHELLLAY.EXP`. Host validation includes the shell-layout service and folder membership for supported system tiles.

## Orbis folder-navigation + DEX metadata pass

Folder selection now mirrors the OrbisOS navigation shown in the 2026-08-09 reference video: a selected folder exposes a compact member preview; opening it animates the member row upward into the main navigation plane, fades the parent Home row, shows a folder breadcrumb, and expands the selected member using the normal Home tile geometry. LEFT/RIGHT navigation uses interpolated focus/position and O reverses the transition back to the folder preview. Game launch from inside a folder preserves the child tile as the launch subject instead of animating the parent folder tile.

DEX Mode now renders a compact Title ID + version strip on Home-grid content only. Native Titan titles use the `TITN` namespace, PS2 games use their real SYSTEM.CNF startup ID, and ISO versions are read from SYSTEM.CNF `VER`. If a source format does not expose a version (for example a plain ul.cfg entry), the UI reports `v?` instead of fabricating metadata. The upper system-menu icons intentionally never render the Title ID strip.

## Native 720p page-flip pass

RC3 replaces the single native scanout/render surface with two page-aligned
PSM16 buffers and presents only at VSync. The exact 4 MiB layout is documented
in `docs/RENDERER_BUDGET.md`. Host contracts cover the allocation and state
transition; physical PS2 timing validation is still required.
