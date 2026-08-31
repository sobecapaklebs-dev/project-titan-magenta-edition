# BuildFix27 — FCEUmm Real Source Integration

STATUS: SOURCE_INTEGRATED_HOST_VALIDATED

## Frozen provenance validation

- repository: libretro/libretro-fceumm
- frozen commit: `b5e3566515c27dc66c9c20572171673126532e06`
- expected Git tree: `6adfb382bf8a369f1a6ee224e89c08d981e22cec`
- supplied archive SHA-256: `8153226f90508b46d03e217e91508f452c6ef0b4ddc797c2fac776f902081f12`
- supplied ZIP comment: exact frozen commit
- Git tree recomputed from extracted ZIP: exact expected tree
- upstream files: 687
- license: GPL-2.0

The uploaded ZIP was converted losslessly to a deterministic tar wrapper only because the existing BF27 importer accepts `.tar.gz`. The existing importer then passed its frozen `modern` provenance/blob checks and imported the source into `third_party/fceumm/modern`.

## Source integration completed

- Project NES target Makefile now compiles the frozen FCEUmm `Makefile.common` portable C source list for R5900 and links it into the ET_REL `.tmod` with `mips64r5900el-ps2-elf-ld -r`.
- `STATIC_LINKING=1`, `HAVE_HDPACK=0`, `HAVE_NTSC=0`, RGB565 frontend support.
- No ARM/NEON path introduced.
- FCEUmm heap calls are redirected to a Titan-owned allocator shim backed by `TitanAppApiV1::alloc/release`; realloc/calloc are implemented without exposing a general allocator import.
- FCEUmm auxiliary read-only filestream/path helpers are backed by Titan sandboxed `file_read/file_exists` for `data/system` assets.
- Libretro `GET_SYSTEM_DIRECTORY` returns `data/system`.
- Libretro `GET_TARGET_SAMPLE_RATE` returns 32000 Hz.
- Libretro `GET_GAME_INFO_EXT` exposes the already-loaded ROM memory buffer; no commercial ROM is bundled.
- FCEUmm GPL v2 text is included in the Project NES package source.
- Native ET_REL resolver gained only side-effect-free libc/libm helpers needed by the portable core; file/device/process/network/allocation symbols remain unavailable.

## Validation executed

PASS:
- existing BF27 FCEUmm importer contract
- existing Project NES FCEUmm adapter contract
- Project NES host suite
- Application Runtime ET_REL/R5900 ISA guard host harness (ASan+UBSan)
- real frozen FCEUmm host compile/link/load/run using ProjectNesTest.nes through the Titan adapter

The real-core UBSan run reports one upstream signed-left-shift warning in `drivers/libretro/libretro.c:1074`; the frozen upstream source was not modified to hide it. The real core still loaded and executed frames successfully. This remains a target/performance observation, not a fabricated PASS for PS2 hardware.

TARGET_BUILD: PENDING_EXTERNAL_PS2DEV
PCSX2: PENDING_EXTERNAL
HARDWARE: PENDING_EXTERNAL
