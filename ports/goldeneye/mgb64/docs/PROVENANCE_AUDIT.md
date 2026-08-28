# Native Support Provenance Audit

This audit records the native-compiled support-code surfaces that previously
carried SDK/demo-lineage comments or use N64 SDK-shaped interfaces. It is not a
claim that the whole repository is clean-room; `THIRD_PARTY.md` remains the
authoritative inventory for SDK/libultra-lineage matching-target material.

## Current Policy

- Native play builds must not compile proprietary SDK implementation source.
- Direct SDK/devkit/demo source-path comments are not allowed in public source.
- SDK-shaped types and constants may remain where they are necessary ABI
  declarations, but provenance belongs in docs rather than implementation
  comments.
- Decompiled original-game systems are kept as game code unless a concrete
  copied external implementation is identified.

## Audited Native Surfaces

| Surface | Native classification | Notes |
| --- | --- | --- |
| `src/audi.c` | Native build delegates to clean-room `src/platform/audi_port.c`; non-native body is matching-target-only. | `NATIVE_PORT` compiles only the port implementation path. Stale devkit/audio-demo comments were removed. |
| `src/snd.c`, `src/snd.h` | Decompiled game sound-player surface with native fixes/instrumentation. | Current native builds use `PORT_SOUNDPLAYER_REAL`, not the legacy stub path. The code relies on the project-owned `src/platform/audio_compat.c` implementation for libaudio-compatible behavior. |
| `src/music.c`, `src/music.h` | Decompiled game music manager plus native ROM-pointer parsing and trace hooks. | Sequence/bank setup remains game-specific. Native pointer parsing avoids depending on host pointer width matching N64 data structures. |
| `src/boss.c` | Decompiled game main loop and frontend/game state orchestration. | Native-specific additions are explicit platform hooks such as direct `joyPoll()` calls when there is no separate N64 polling thread. |
| `src/fr.c` | Decompiled game frame/video state glue. | Native-facing changes are pointer-width/framebuffer compatibility adjustments. |
| `src/joy.c` | Decompiled game controller manager with native polling compatibility. | Native builds skip the N64 controller thread wait and poll through the platform input path. |
| `src/game/rsp.c`, `src/game/rsp.h` | Decompiled game graphics-task wrapper with a native direct-render path. | Native builds translate display lists and send completion messages without scheduling real RSP/RDP hardware tasks. |
| `include/PR/sptask.h` | Native build does not use this body; `platform_os.h` supplies task types. | The non-native branch remains an SDK-shaped ABI compatibility header for the matching target. |
| `src/rmon.c`, `src/rmon.h` | Native build uses a clean-room remote-monitor shim. | Native `rmon` entry points now provide token/no-op host I/O and formatted debug text through the crash text buffer without compiling the non-native monitor body. |
| `src/sched.c`, `src/sched.h` | Decompiled original-game scheduler/task-dispatch code. | The scheduler retains SDK-shaped task/message interfaces because the original game is built around those contracts. Direct SDK/sample-code comments were removed; future conservative cleanup can replace the native scheduler with a platform-owned implementation if desired. |

## Additional Cleanup From This Audit

- Removed unused legacy headers `include/PR/gs2dex.h`, `include/PR/primage.h`,
  `include/PR/rdb.h`, `include/PR/ram_ORIGINAL_rom.h`,
  `include/PR/rm_ORIGINAL_on.h`, and `include/PR/libultra.h`. They had no
  tracked includes or were trivial wrappers, and increased public provenance
  surface.
- Removed `include/PR/ultraerror.h` after localizing the one remaining synth
  diagnostic ID used by clean-room matching-target audio wrappers.
- Strengthened `scripts/ci/check_no_rom_data.sh` so direct SDK/devkit/demo
  source breadcrumbs in public source fail the contamination guard.
- Added `tools/check_sdk_inventory.py` as the release-guarded inventory for
  tracked SDK-shaped compatibility paths under `include/PR/`, `src/libultra/`,
  and `src/libultrare/`. New files in those directories now fail public release
  validation until they are reviewed and added deliberately.
- Extended `tools/check_sdk_inventory.py` to guard matching-target libaudio
  linker placement. The linker scripts must place
  `src/libultra/audio/clean_compat.o` and must not reference removed historical
  SDK/Rare libaudio objects.
- Narrowed native CMake include directories so the native executable and macOS
  static-library target no longer search broad `src/libultra/`,
  `src/libultra/gu/`, or `src/libultrare/audio/` directories. The native target
  keeps only the narrow `src/libultra/audio/` path needed for the clean-room
  audio compatibility helper headers, and `tools/check_native_sdk_surface.py`
  fails if the broad include roots return.

## Residual Risk

The native build no longer compiles known proprietary-notice-bearing SDK source,
and this audit did not identify a native-compiled external SDK/demo
implementation that blocks the current public source release. The most
conservative remaining provenance work is still the broader SDK/libultra-lineage
material kept for the in-progress N64 matching target, plus future replacement
of any SDK-shaped native declarations that can be retired without breaking the
game.
