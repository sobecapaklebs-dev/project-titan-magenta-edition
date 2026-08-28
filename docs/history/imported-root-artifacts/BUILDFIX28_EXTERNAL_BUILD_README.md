# BuildFix28 External Build — Garuda Linux + PS2DEV/PS2SDK

These commands correspond to the delivered BF28 source tree. They intentionally do not use sandbox-specific paths.

## 1. Extract and enter the source

```bash
mkdir -p ~/Projects/project-titan/bf28
cd ~/Projects/project-titan/bf28
tar -xzf ~/Downloads/Project-Titan-BuildFix28-FINAL-SOURCE.tar.gz
cd Project-Titan-BuildFix28-FINAL-SOURCE
```

Verify the archive first if the `.sha256` file is beside it:

```bash
cd ~/Downloads
sha256sum -c Project-Titan-BuildFix28-FINAL-SOURCE.tar.gz.sha256
```

## 2. Garuda/Arch prerequisites

```bash
sudo pacman -S --needed base-devel git make cmake ninja python python-pip \
  bison flex texinfo gmp mpfr libmpc zlib
python -m pip install --user Pillow
```

## 3. PS2DEV environment

The source lock expects:

- ps2dev repository commit `0463697a324b3d3c000636e6fd12324b0f2a8df2`
- ps2toolchain commit `6d63befdde76fd695435d16f246a3bde72a8c096`
- ps2sdk commit `06021a615a908360491ddad198ce6fcf0eddcbde`
- EE GCC `15.2.0`

Set the environment for the installation you will use:

```bash
export PS2DEV="$HOME/ps2dev"
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"

mips64r5900el-ps2-elf-gcc --version
mips64r5900el-ps2-elf-gcc -dumpmachine
```

Expected machine: `mips64r5900el-ps2-elf`.

When the PS2DEV checkout contains the component Git repositories, verify the lock:

```bash
git -C "$PS2DEV" rev-parse HEAD
git -C "$PS2DEV/build/ps2toolchain" rev-parse HEAD
git -C "$PS2DEV/build/ps2sdk" rev-parse HEAD
```

## 4. Required PFS IRXs

The kernel automatically enables embedded PFS only when all three files exist:

```bash
ls -l \
  "$PS2SDK/iop/irx/ps2atad.irx" \
  "$PS2SDK/iop/irx/ps2hdd.irx" \
  "$PS2SDK/iop/irx/ps2fs.irx"
```

Do not substitute fabricated IRXs. If your PS2SDK build does not provide these files, PFS target validation remains pending until the legitimate binaries are installed.

## 5. Host regression before target build

```bash
bash -n kernel/tests/run_host_tests.sh
bash kernel/tests/run_host_tests.sh
```

## 6. Full target build

```bash
JOBS="$(nproc)" bash scripts/build_all.sh
```

For the reproducibility gate used by the release build:

```bash
JOBS="$(nproc)" bash scripts/build_all.sh --verify-reproducible
```

The runtime package is written to:

```text
dist/Project Titan Magenta Edition/
```

## 7. Kernel only

```bash
make -C bootloader clean clean-generated all
make -C kernel clean clean-generated all
```

Expected primary artifacts:

```text
bootloader/titan.elf
kernel/kernel.elf
```

## 8. Project NES / FCEUmm only

The real frozen FCEUmm source is already present in this final archive. Do not select another provider.

```bash
make -C apps/project-nes clean
make -C apps/project-nes host-test
make -C apps/project-nes all verify \
  EE_CC="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" \
  EE_LD="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-ld"
```

Expected package:

```text
apps/project-nes/build/PROJECTNES.pkg
```

## 9. wLaunchELF native package

```bash
make -C apps/wlaunchelf clean
make -C apps/wlaunchelf host-test
make -C apps/wlaunchelf all verify \
  EE_CC="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"
```

Expected package:

```text
apps/wlaunchelf/build/WLAUNCHELF.pkg
```

## 10. Titan-Neutrino and Titan UL engines

The full build performs the canonical engine commands. To rebuild manually, use the same commands from `scripts/build_all.sh`:

```bash
make -C modules/titan-neutrino-engine clean GIT_TAG="titan-$(cat VERSION)"
make -C modules/titan-neutrino-engine -j"$(nproc)" all GIT_TAG="titan-$(cat VERSION)"
make -C modules/titan-neutrino-engine/ee/loader clean all DEBUG=0 GIT_TAG="titan-$(cat VERSION)"
make -C modules/titan-neutrino-engine copy
JOBS="$(nproc)" bash scripts/build_titan_ul_engine.sh
```

## 11. BF27 CP47 target evidence orchestrator retained by BF28

For the historical target evidence workflow:

```bash
python3 scripts/run_buildfix27_target_validation.py --help
```

Use an actual prepared PS2DEV bundle/evidence directory as required by the script. It must remain fail-closed; do not replace target steps with host compilers.

An optional PCSX2 command is caller-supplied, for example only after confirming your installed PCSX2 CLI syntax:

```bash
python3 scripts/run_buildfix27_target_validation.py \
  --bundle-root /path/to/validated-bundle \
  --evidence-dir "$HOME/Projects/titan-bf28-evidence/target-pcsx2" \
  --require-complete \
  --pcsx2-cmd 'pcsx2-qt -batch {kernel}'
```

## 12. Physical PS2 validation

Follow `BUILDFIX28_HARDWARE_TEST_PLAN.md`. Record the build/archive SHA, console/video mode, controller/transport, title/engine, overlay Resume/Return result, input/audio/video/save observations, boot/reboot/shutdown/recovery results, and any serial/debug logs or screenshots.

Hardware authority remains the physical PlayStation 2.
