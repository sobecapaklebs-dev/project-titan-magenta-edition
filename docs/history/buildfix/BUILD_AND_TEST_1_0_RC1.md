# Build and Test — Project Titan 1.0 RC1 Hardware Candidate

## Build

```sh
cd /path/to/Project-Titan-1.0-RC1
export PS2DEV="$HOME/ps2dev"   # use your actual PS2DEV root
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"

bash scripts/build_all.sh --verify-reproducible
```

If your PS2DEV is installed under `/opt/ps2dev`, substitute that path.

## Host gates worth rerunning before target build

```sh
python3 kernel/tests/test_release_contract.py kernel
python3 kernel/tests/test_source_hardening_contract.py .
python3 kernel/tests/test_bf30_hwfix2_performance_regression_contract.py kernel
python3 kernel/tests/test_bf30_hwfix2_input_bt_welcome_contract.py .
python3 kernel/tests/test_bf30_hwfix2_perf_tech_observability_contract.py .
python3 kernel/tests/test_bf30_vlombis_full_integration_contract.py kernel
```

The full host runner is:

```sh
bash kernel/tests/run_host_tests.sh
```

On the inherited BF30 hardware/build tree the open-source-readiness/source-clean gate is expected to flag the pre-existing compiled payload set. The supplied BF30 baseline and this RC report the same 80 payload/hash errors. Do not remove required target/runtime inputs merely to hide that inherited package-state exception.

## Install

After the target build succeeds, install the **newly rebuilt** runtime/installer. Do not use the inherited BF30 `kernel.elf`/`dist` output as RC1.

The existing project helper remains:

```sh
bash scripts/install_to_usb.sh --target "/run/media/$USER/PS"
```

Use the same proven installer/boot chain you used for the successful BF30 hardware install.

## First hardware capture

1. Boot normally and verify Welcome X / Player 1.
2. Open Settings > System and confirm:
   - `pikaVR` shows ACTIVE;
   - `VlombisCore` shows READY/ACTIVE and N/10 measured domains.
3. Enable the DEX Performance Overlay for the first performance pass.
4. Navigate HOME -> Settings -> Library -> HOME repeatedly.
5. Enter Library twice with no media changes; the second pass should increase Library cache/metadata-reuse counters.
6. Run a Library refresh while audio and network are active.
7. Test Controllers -> Bluetooth -> Pair wireless controller.
8. Export the debug report and persistent diagnostics.

The decisive comparison is against the BF30 regression session: 14.27 FPS / ~70 ms average / ~80 ms render with only ~48 KiB free in the bad 720p path. RC1 should no longer be on that Reference layout and should recover the previously observed smooth navigation/Flow behavior.
