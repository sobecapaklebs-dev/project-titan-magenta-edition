# BuildFix29 External Build — Garuda Linux + PS2DEV/PS2SDK

These commands target the delivered BuildFix29 final source archive and intentionally avoid executor-specific paths.

## 1. Verify and extract

```bash
cd ~/Downloads
sha256sum -c Project-Titan-BuildFix29-FINAL-SOURCE.tar.gz.sha256
mkdir -p ~/Projects/project-titan/bf29
cd ~/Projects/project-titan/bf29
tar -xzf ~/Downloads/Project-Titan-BuildFix29-FINAL-SOURCE.tar.gz
cd Project-Titan-BuildFix29-FINAL-SOURCE
sha256sum -c BUILDFIX29_FINAL_SOURCE_MANIFEST.sha256
```

## 2. Garuda/Arch prerequisites

```bash
sudo pacman -S --needed base-devel git make cmake ninja python python-pip \
  bison flex texinfo gmp mpfr libmpc zlib
python -m pip install --user Pillow
```

## 3. PS2DEV environment lock inherited from BuildFix28

- ps2dev repository commit `0463697a324b3d3c000636e6fd12324b0f2a8df2`
- ps2toolchain commit `6d63befdde76fd695435d16f246a3bde72a8c096`
- ps2sdk commit `06021a615a908360491ddad198ce6fcf0eddcbde`
- EE GCC `15.2.0`

```bash
export PS2DEV="$HOME/ps2dev"
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"
mips64r5900el-ps2-elf-gcc --version
mips64r5900el-ps2-elf-gcc -dumpmachine
```

Expected machine: `mips64r5900el-ps2-elf`.

## 4. PFS driver availability

```bash
ls -l \
  "$PS2SDK/iop/irx/ps2atad.irx" \
  "$PS2SDK/iop/irx/ps2hdd.irx" \
  "$PS2SDK/iop/irx/ps2fs.irx"
```

Do not substitute fabricated IRXs. Missing legitimate binaries keep PFS target validation pending.

## 5. Host regression

```bash
bash -n kernel/tests/run_host_tests.sh
bash kernel/tests/run_host_tests.sh
```

## 6. Full target build

```bash
JOBS="$(nproc)" bash scripts/build_all.sh
JOBS="$(nproc)" bash scripts/build_all.sh --verify-reproducible
```

Primary runtime output remains under `dist/Project Titan Magenta Edition/` according to the project build scripts.

Target/PCSX2/hardware PASS must be recorded only from the corresponding external environment.
