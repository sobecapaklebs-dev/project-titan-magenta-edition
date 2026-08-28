# Titan Shell and Runtime 0.3.91-rc15

This directory contains the Project Titan Magenta Edition kernel/runtime:
renderer, shell, profiles, VMC, library, applications, DEX diagnostics, audio,
input and handoff services.

## Runtime engine paths

```text
mass:/neutrino/neutrino.elf
mass:/TITAN/titan_ul_engine.elf
```

ISO games use Titan Neutrino Engine. UL games use Titan UL Engine. Both receive
the active profile VMC explicitly. External applications pass through the
shared ELF validator and low-memory stage2.

## Host validation

```sh
../scripts/validate_source.sh
```

## MIPS build

```sh
make clean all
```

Generated headers are created from the raw UI assets and PS2SDK modules at
build time. They are intentionally absent from Git/source archives.

RC15 preserves the RC14 runtime and adds the append-only TGR v2 texture-release
ABI, bounded game-texture arena and Ribbon v2 deformation path. Its GoldenEye
frontier is host-tested source only; renderer/audio/IOP behavior and the full
TitanEYE game path still require a PS2DEV build and physical PS2 validation.
