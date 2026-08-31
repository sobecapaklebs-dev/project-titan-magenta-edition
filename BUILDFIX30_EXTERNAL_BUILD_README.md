# BuildFix30 External Build / Target Validation

## Verify and extract TARGETFIX8 telemetry closure
```bash
cd ~/Downloads
sha256sum -c Project-Titan-BuildFix30-TARGETFIX8-TELEMETRY-CLOSURE.tar.gz.sha256
mkdir -p ~/Projects/project-titan/bf30 && cd ~/Projects/project-titan/bf30
tar -xzf ~/Downloads/Project-Titan-BuildFix30-TARGETFIX8-TELEMETRY-CLOSURE.tar.gz
cd Project-Titan-BuildFix30-TARGETFIX8-TELEMETRY-CLOSURE
sha256sum -c BUILDFIX30_TARGETFIX8_SOURCE_MANIFEST.sha256
```

## Garuda/Arch prerequisites
```bash
sudo pacman -S --needed base-devel git make cmake ninja python python-pip bison flex texinfo gmp mpfr libmpc zlib
python -m pip install --user Pillow
```

## Frozen PS2DEV environment inherited from BF29/BF28
- ps2dev `0463697a324b3d3c000636e6fd12324b0f2a8df2`
- ps2toolchain `6d63befdde76fd695435d16f246a3bde72a8c096`
- ps2sdk `06021a615a908360491ddad198ce6fcf0eddcbde`
- EE GCC `15.2.0`

```bash
export PS2DEV="$HOME/ps2dev"
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"
mips64r5900el-ps2-elf-gcc --version
mips64r5900el-ps2-elf-gcc -dumpmachine
bash -n kernel/tests/run_host_tests.sh
bash kernel/tests/run_host_tests.sh
JOBS="$(nproc)" bash scripts/build_all.sh --diagnostic-sweep
JOBS="$(nproc)" bash scripts/build_all.sh
JOBS="$(nproc)" bash scripts/build_all.sh --verify-reproducible
```
Expected target machine: `mips64r5900el-ps2-elf`.

Record target build, PCSX2 and physical evidence independently. Required physical coverage includes BF30 boot regression, 480i/720p, complete controller/network/storage matrix, Theme pre-HOME/HOME/Return lifecycle, original PS2 CD/DVD Disc Service/Engine launch, per-title Overlay/IGR negotiation, PUP auto-reboot, shutdown/reboot/recovery.


## CP19 integrated FMCB installer

The FMCB engine is linked into `titan-installer.elf`; do not build or package a separate FMCB installer ELF.

```bash
python3 tools/build_titan_fmcb.py --project . --output build/fmcb
make -C kernel installer TITAN_AR9271_FW="$TITAN_AR9271_FW"
python3 tools/build_titan_installer_media.py \
  --project . \
  --runtime "dist/Project Titan Magenta Edition" \
  --installer-elf titan-installer.elf \
  --bootloader-elf titan.elf \
  --fmcb-build build/fmcb \
  --output build/titan-installer-usb
```

`build/fmcb` contains the static integrated backend archive plus `titan-fmcb-entry.elf`. The USB release contains the FMCB resource tree needed by the integrated engine, but no second installer executable.
