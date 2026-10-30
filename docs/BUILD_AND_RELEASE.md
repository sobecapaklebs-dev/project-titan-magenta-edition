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

If the raw AR9271 firmware is not available, the target preflight accepts an
external validated frozen IRX. Set `TITAN_AR9271_FROZEN_IRX` to its absolute
path; the preflight and kernel build both verify SHA-256
`6bc9275efbc1de6e643575867a0e14dac02a3a6cf0d5d6ea12906ac7ef73aa17`, and the
clean temporary build copies that artifact into the target tree. Otherwise,
provide the raw firmware as `TITAN_AR9271_FW=/absolute/path/to/htc_9271-1.4.0-titan-data-crypto-tt1424.fw`.

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
