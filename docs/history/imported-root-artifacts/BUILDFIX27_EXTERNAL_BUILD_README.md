# BuildFix27 — External PS2DEV / PS2SDK Build Instructions

These commands are intended for Garuda Linux / Arch-family hosts. Install the basic host prerequisites first:

```bash
sudo pacman -S --needed base-devel git python
```

These commands correspond to the final BF27 tree. They target PlayStation 2 Emotion Engine / MIPS R5900 and do not use ARM/NEON paths.

## A. Extract and verify the delivered source

```bash
mkdir -p ~/Projects/project-titan-bf27
cd ~/Projects/project-titan-bf27

cp ~/Downloads/Project-Titan-BuildFix27-TARGET-READY-SOURCE-WITH-FCEUMM.tar.gz .
cp ~/Downloads/Project-Titan-BuildFix27-TARGET-READY-SOURCE-WITH-FCEUMM.tar.gz.sha256 .
sha256sum -c Project-Titan-BuildFix27-TARGET-READY-SOURCE-WITH-FCEUMM.tar.gz.sha256

tar -xzf Project-Titan-BuildFix27-TARGET-READY-SOURCE-WITH-FCEUMM.tar.gz
cd Project-Titan-BuildFix27-TARGET-READY-SOURCE-WITH-FCEUMM

# If BUILDFIX27_FINAL_MANIFEST.txt was downloaded beside the archive:
sha256sum -c ../BUILDFIX27_FINAL_MANIFEST.txt
```

If you extracted the tree somewhere else, run the manifest check from the source root.

## B. PS2DEV / PS2SDK environment

The BF27 build expects an installed PS2DEV tree containing:

```text
$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc
$PS2DEV/ee/bin/mips64r5900el-ps2-elf-g++
$PS2DEV/ee/bin/mips64r5900el-ps2-elf-ar
$PS2DEV/ee/bin/mips64r5900el-ps2-elf-ld
$PS2DEV/dvp/bin/dvp-as
$PS2SDK/iop/irx/...
```

For the common layout `~/ps2dev`:

```bash
export PS2DEV="$HOME/ps2dev"
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2DEV/bin:$PATH"

mips64r5900el-ps2-elf-gcc -dumpmachine
mips64r5900el-ps2-elf-gcc --version
```

The dumpmachine result required by Project NES/wLaunchELF is:

```text
mips64r5900el-ps2-elf
```

## C. Validate the PS2DEV bundle and emit the exact BF27 environment

```bash
mkdir -p ~/Projects/titan-bf27-evidence
python3 scripts/validate_ps2dev_bundle.py \
  "$PS2DEV" \
  --require-ready \
  --emit-env ~/Projects/titan-bf27-evidence/bf27-target-env.sh

source ~/Projects/titan-bf27-evidence/bf27-target-env.sh
```

The validator accepts `$PS2DEV`, a parent containing `ps2dev/`, or a bundle with `usr/local/ps2dev/`.

## D. Verify the frozen FCEUmm already included in this source archive

Do **not** replace or update the core.

```bash
test -f third_party/fceumm/modern/Copying
sha256sum -c external/fceumm/FCEUMM_IMPORTED_SHA256SUMS

grep -F 'b5e3566515c27dc66c9c20572171673126532e06' \
  apps/project-nes/resources/FCEUMM_SOURCE_PROVENANCE.txt

grep -F '6adfb382bf8a369f1a6ee224e89c08d981e22cec' \
  apps/project-nes/resources/FCEUMM_SOURCE_PROVENANCE.txt
```

Frozen identity:

```text
repository: https://github.com/libretro/libretro-fceumm
commit: b5e3566515c27dc66c9c20572171673126532e06
Git tree: 6adfb382bf8a369f1a6ee224e89c08d981e22cec
license: GNU GPL v2
```

No external FCEUmm import is required for this final archive because the frozen source is already materialized.

## E. PFS / HDD prerequisites

BF27 expects the official PS2SDK drivers at:

```text
$PS2SDK/iop/irx/ps2atad.irx
$PS2SDK/iop/irx/ps2hdd.irx
$PS2SDK/iop/irx/ps2fs.irx
```

Verify them before the full target build:

```bash
python3 scripts/validate_pfs_target_prereqs.py --require-ready
```

Do not substitute arbitrary IRXs if the validator fails.

## F. Project NES host validation before target compilation

```bash
make -C apps/project-nes host-test

python3 kernel/tests/test_buildfix27_project_nes_conformance_contract.py "$PWD"
python3 kernel/tests/test_buildfix27_project_nes_fceumm_adapter_contract.py "$PWD"
python3 kernel/tests/test_buildfix27_project_nes_lifecycle_package_contract.py "$PWD"
python3 kernel/tests/test_buildfix27_project_nes_provider_contract.py "$PWD"
python3 kernel/tests/test_buildfix27_project_nes_storage_save_contract.py "$PWD"
python3 kernel/tests/test_buildfix27_fceumm_importer_contract.py "$PWD"
```

## G. Individual Project NES target build

```bash
make -C apps/project-nes clean
make -C apps/project-nes all verify \
  EE_CC="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" \
  EE_LD="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-ld"
```

Expected artifacts:

```text
apps/project-nes/build/PROJECTNES.pkg
apps/project-nes/build/source/project_nes.tmod
```

The target build links the frozen FCEUmm provider; the Legacy NROM core is not the BF27 release provider.

## H. Individual wLaunchELF/native target build

```bash
make -C apps/wlaunchelf clean
make -C apps/wlaunchelf all verify \
  EE_CC="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"
```

Expected artifacts:

```text
apps/wlaunchelf/build/WLAUNCHELF.pkg
apps/wlaunchelf/build/source/wlaunchelf.tmod
```

## I. Individual kernel target build

```bash
make -C kernel clean
make -C kernel all
```

Expected artifact:

```text
kernel/kernel.elf
```

## J. Complete CP47 target build orchestration

Preferred full command:

```bash
rm -rf ~/Projects/titan-bf27-evidence/target
mkdir -p ~/Projects/titan-bf27-evidence/target

python3 scripts/run_buildfix27_target_validation.py \
  --root "$PWD" \
  --bundle-root "$PS2DEV" \
  --evidence-dir ~/Projects/titan-bf27-evidence/target
```

On success inspect:

```bash
cat ~/Projects/titan-bf27-evidence/target/target-validation.json
cat ~/Projects/titan-bf27-evidence/target/target-validation.log
```

CP47 builds and hashes:

```text
kernel/kernel.elf
apps/wlaunchelf/build/WLAUNCHELF.pkg
apps/wlaunchelf/build/source/wlaunchelf.tmod
apps/project-nes/build/PROJECTNES.pkg
apps/project-nes/build/source/project_nes.tmod
```

## K. Optional PCSX2 smoke step through CP47

Only use a PCSX2 command you have already verified on your machine. CP47 substitutes `{kernel}` with the built kernel path.

Example for a CLI-compatible local PCSX2 installation:

```bash
python3 scripts/run_buildfix27_target_validation.py \
  --root "$PWD" \
  --bundle-root "$PS2DEV" \
  --evidence-dir ~/Projects/titan-bf27-evidence/target-pcsx2 \
  --pcsx2-cmd 'pcsx2-qt -batch {kernel}'
```

Do not mark PCSX2 validated if your installed PCSX2 does not support that invocation or the smoke run does not execute successfully.

## L. Full host regression

```bash
bash kernel/tests/run_host_tests.sh | tee ~/Projects/titan-bf27-evidence/host-regression.log
```

## M. Hardware deployment / CP48 evidence

Use the existing Titan deployment path appropriate to your current installation, preserving the known-functional boot chain. Copy only target artifacts produced by the verified build.

Before testing, record hashes:

```bash
sha256sum \
  kernel/kernel.elf \
  apps/project-nes/build/PROJECTNES.pkg \
  apps/project-nes/build/source/project_nes.tmod \
  apps/wlaunchelf/build/WLAUNCHELF.pkg \
  apps/wlaunchelf/build/source/wlaunchelf.tmod \
  | tee ~/Projects/titan-bf27-evidence/target-artifacts.sha256
```

For Project NES hardware validation test at minimum:

```text
1. Install/launch Project NES through Titan.
2. Run the bundled ProjectNesTest.nes diagnostic ROM.
3. Confirm stable video, input and audio.
4. Reset the core.
5. Test save RAM with a legal/user-provided battery-backed NES image where applicable.
6. Exit with Select + Circle and confirm clean return to Titan.
7. Repeat launch/exit cycles and watch for resource/lifecycle regressions.
```

## N. CP48 evidence collection

Follow:

```text
BUILDFIX27_HARDWARE_EVIDENCE_FORMAT.md
```

Return these files/results to the Project Titan development conversation:

```text
~/Projects/titan-bf27-evidence/target/target-validation.json
~/Projects/titan-bf27-evidence/target/target-validation.log
~/Projects/titan-bf27-evidence/target-artifacts.sha256
hardware PASS/FAIL observations
serial/debug output where available
screenshots/photos where useful
```

Never report `HARDWARE_VALIDATED` without a physical PS2 run.

## O. Deterministic source recovery from persisted BF27 material

If you need to reconstruct the frozen BF27 source from the persisted recovery artifacts rather than the full source archive, place CP00, CP10R, CP11..CP49, `BUILDFIX27_FCEUMM_REAL_INTEGRATION.tar.gz`, `BUILDFIX27_FINAL_CLOSURE_DELTA.tar.gz`, and `BUILDFIX27_FINAL_MANIFEST.txt` in one directory, then run:

```bash
python3 scripts/rehydrate_buildfix27.py \
  --material-dir "$HOME/Projects/titan-bf27-material" \
  --manifest "$HOME/Projects/titan-bf27-material/BUILDFIX27_FINAL_MANIFEST.txt" \
  --output "$HOME/Projects/Project-Titan-BuildFix27-TARGET-READY-SOURCE-WITH-FCEUMM" \
  --overwrite
```

A successful reconstruction prints:

```text
MISSING=0 UNEXPECTED=0 HASH_MISMATCH=0
BUILDFIX27_FINAL_REHYDRATE_OK
```
