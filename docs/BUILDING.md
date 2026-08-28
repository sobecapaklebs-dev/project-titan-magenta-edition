# Building Project Titan

## Prerequisites

A Linux host with a working PS2DEV toolchain is required for target builds.

Expected environment:

```bash
export PS2DEV="$HOME/Projects/ps2dev"
export PS2SDK="$PS2DEV/ps2sdk"
export GSKIT="$PS2DEV/gsKit"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PATH"
```

Sanity check:

```bash
command -v mips64r5900el-ps2-elf-gcc
mips64r5900el-ps2-elf-gcc -dumpmachine
```

The expected target is:

```text
mips64r5900el-ps2-elf
```

## Main build

From the repository root:

```bash
bash scripts/build_all.sh
```

For debugging a kernel failure, prefer a serial build so the first error is easy to identify:

```bash
make -C kernel -j1 all 2>&1 | tee ~/Downloads/titan-kernel-build.log
```

If the AR9271 module is rebuilt, provide the firmware path only when the build system requires it:

```bash
make -C kernel -j1 all TITAN_AR9271_FW=/path/to/htc_9271-1.4.0.fw
```

Do not commit the firmware blob to the public repository unless its redistribution terms explicitly permit it.


## Bootloader release artifact

The Memory Card bootloader release artifact is:

```text
BOOT.ELF
```

The name is case-sensitive for the supported DEV1/FMCB boot contract. A release build must not regress the Memory Card artifact to `titan.elf`.

The initial public installer media is Semi-autonomous only. When building that media, do **not** pass an autonomous/FMCB payload into the release-media builder.

## Native applications

Native application make targets differ by application. Typical examples:

```bash
make -C apps/project-nes all
make -C apps/project-nes verify

make -C apps/wlaunchelf all
make -C apps/wlaunchelf verify
```

The package builder must accept the current asset contract:

```text
background.rgb565  640x480 primary, 512x256 legacy fallback
tile.rgb565        256x256
icon.rgb565         64x64
```

## Private game ports

Never add commercial ROMs or derived game data to the public source tree. A private port may reference a user-supplied ROM path at build time, but the ROM itself and any package embedding it must remain outside the public repository.

## Host contracts

Run the repository's host tests before publishing:

```bash
bash kernel/tests/run_host_tests.sh
```

Also run Python syntax checks for package/build helpers you changed:

```bash
python3 -m py_compile path/to/tool.py
```

## Build artifacts

Target binaries and packages are build outputs, not source-control content. Keep them out of normal commits unless a dedicated GitHub Release explicitly distributes a redistributable binary.

Examples:

```text
*.elf
*.irx
*.o
*.a
*.tmod
*.tpkg
*.pkg
*.pup
build/
build-*/
```
