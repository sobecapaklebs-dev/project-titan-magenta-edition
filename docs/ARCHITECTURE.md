# Project Titan Magenta Edition architecture

Project Titan Magenta Edition is a PS2 system platform, not a collection of
unrelated launchers. The maintained tree has four product components:

- **Titan Bootloader**: validates and hands off to the canonical root kernel.
- **Titan Shell and Runtime** (`kernel/`): UI, profiles, VMC, library, audio,
  renderer, applications, DEX diagnostics and service lifecycle.
- **Titan Neutrino Engine**: the maintained Neutrino-derived ISO backend.
- **Titan UL Engine**: the maintained Open PS2 Loader-derived UL backend.

The two engines retain their upstream licences, notices and authorship. The
Titan name identifies the modified, integrated variants maintained by this
project; it does not claim authorship of their upstream foundations.

## Canonical deployment paths

```
mass:/titan.elf
mass:/kernel.elf
mass:/neutrino/neutrino.elf
mass:/TITAN/titan_ul_engine.elf
mass:/TITAN/audio/
mass:/TITAN/packages/
mass:/TITAN/apps/TITLEID/
```

`mass:/TITAN/kernel.elf` and `titan_ul_loader.elf` are legacy experimental
names. The installer disables rather than silently overwrites those copies.

## Core contracts

- External ELFs use the shared strict validator and the low-memory stage2.
- Persistent files use temporary + verified backup publication.
- Scanners build a candidate catalog and publish only a complete valid result.
- Frame-facing services are cooperative and have bounded work per update.
- TPKG applications are checksum-verified in a hidden USB staging directory,
  validated as MIPS ELF, then renamed into the application catalog atomically.
- 720p uses one PSM16 surface anchored immediately after VSync; it does not
  pretend to provide double buffering that does not fit the current VRAM map.
