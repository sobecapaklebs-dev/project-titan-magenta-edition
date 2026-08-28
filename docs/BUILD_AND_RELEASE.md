# Build and release

## Host gate

```sh
scripts/validate_source.sh
```

This runs the kernel host suite, strict bootloader ELF/path tests, the isolated
Titan Neutrino UL-chain compile, installer transaction recovery and deterministic
asset regeneration comparisons.

## Clean MIPS build

`TOOLCHAIN.lock` records the confirmed user toolchain baseline.
`scripts/build_all.sh` exports deterministic locale/time inputs, creates a clean
`git archive` worktree and builds bootloader, stage2, kernel, Titan Neutrino
Engine and Titan UL Engine without mutating the maintained sources.

```sh
JOBS=8 scripts/build_all.sh
JOBS=8 scripts/build_all.sh --verify-reproducible
```

The verification mode builds twice in separate temporary trees and compares all
runtime hashes. Each runtime contains `BUILD_MANIFEST.txt` with source commit,
tool versions and output hashes.

## Installation

```sh
scripts/install_to_usb.sh --target "/run/media/$USER/PS"
```

The installer stages and hashes the whole package, writes a recovery journal,
uses sibling renames, restores interrupted prior transactions before starting,
and publishes only the canonical root `kernel.elf`. Legacy secondary kernel/UL
names are preserved under disabled recovery names.

## Source package

```sh
scripts/package_source.sh
```

The source package uses `git archive`; ignored ELFs, IRXs, objects, generated
headers, caches, backup attempts and repository metadata cannot enter it.
