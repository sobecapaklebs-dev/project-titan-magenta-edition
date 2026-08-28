# BuildFix27 CP40 — PS2 HDD/PFS IOP Provenance

STATUS: SOURCE_PROVENANCE_VERIFIED / TARGET_BINARY_BLOCKED

Official source audited: `ps2dev/ps2sdk` ref `a13b5971ec0e39c7ba8b8559b80a4e81c8425352`.

## APA/HDD driver
- Source build: `iop/hdd/apa/Makefile`.
- Official output: `ps2hdd.irx`.
- Default block path enables DEV9 + ATAD (`APA_USE_DEV9=1`, `APA_USE_ATAD=1`).
- Core imports include IOMANX/loadcore/threadman and the optional/default DEV9 + ATAD interfaces.

## PFS filesystem driver
- Source build: `iop/hdd/pfs/Makefile`.
- Official output: `ps2fs.irx`.
- IOP module identity from `pfs.c`: `IRX_ID("pfs_driver", PFS_MAJOR, PFS_MINOR)`.
- Registers the filesystem device name `pfs` through IOMANX.
- Build imports include CDVDMAN, ATAD, POWEROFF, IOMANX, loadcore, stdio, sysclib, sysmem and threadman and links libpfs objects.

## BuildFix27 target rule
The CP39 platform adapter is intentionally *not* an IRX loader. It becomes available only after both `hdd` and `pfs` IOMANX devices already exist. This preserves Titan IOP ownership and prevents an app from loading arbitrary modules.

The current BuildFix27 workspace contains neither `ps2hdd.irx` nor `ps2fs.irx`, and the current executor has no PS2DEV/PS2SDK/IOP compiler. Therefore no binary bytes are fabricated or claimed.

`scripts/validate_pfs_target_prereqs.py` is the material target gate. On a PS2DEV machine it checks the exact source Makefiles, IOP compiler and non-empty `$(PS2SDK)/iop/irx/ps2hdd.irx` + `ps2fs.irx`. `--require-ready` is the release-gate mode.

## CP41 optional Titan-owned loader path
Official ATAD output is `ps2atad.irx` (`iop/dev9/atad/Makefile`, module name `atad`) and uses DEV9 by default. When `ps2atad.irx`, `ps2hdd.irx` and `ps2fs.irx` all materially exist under `$(PS2SDK)/iop/irx`, the kernel Makefile enables `TITAN_HAVE_PFS_EMBEDDED` and bin2c owns those bytes in `titan_iop_modules.c`. The lazy platform path checks resident module names and executes only missing modules in dependency order: `dev9` -> `atad` -> `hdd_driver` -> `pfs_driver`. It then requires both IOMANX devices `hdd` + `pfs` before advertising availability. Builds missing any of the three additional IRX remain fail-closed and do not generate or reference fabricated bytes.
