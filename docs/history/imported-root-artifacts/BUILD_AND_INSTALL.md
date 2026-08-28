# BuildFix18 build, verification and installation

## Delivered state

This source checkpoint contains the TGR v2/GoldenEye port frontier and four
installable Ribbon v8 themes. It does not contain a GoldenEye ROM. It also does
not publish a TitanEYE executable TPKG: the missing Fast3D-to-GS material
backend, SDL/POSIX-to-TGR replacement, direct-ROM-user conversion and tested
in-process loader make such a package non-executable today. The authoritative
gate list is `ports/goldeneye/PORT_STATUS.md`.

## Host verification

From the source root:

```sh
bash kernel/tests/run_host_tests.sh
python3 -m py_compile \
  kernel/tools/titan_pkg.py \
  apps/wlaunchelf/tools/titan_pkg.py \
  kernel/tests/test_titan_game_runtime_contract.py \
  kernel/tests/test_ribbon_reference_contract.py
```

The complete host suite passed in the delivery environment. This does not
substitute for a PS2 build or runtime test.

## Complete Project Titan PS2 build

Install the toolchain revisions recorded in `TOOLCHAIN.lock`, then export the
canonical PS2DEV variables and run the deterministic build:

```sh
export PS2DEV=/opt/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"
bash scripts/build_all.sh --verify-reproducible
```

The expected runtime output is `dist/Project Titan Magenta Edition/`. Install
that runtime only through the journaled installer:

```sh
bash scripts/install_to_usb.sh --target "/run/media/$USER/PS"
```

The delivery environment did not contain PS2DEV, so this PS2 build command was
not executed here.

## Ribbon theme rebuild

```sh
python3 kernel/tools/titan_pkg.py build --source themes/ribbon-cyan \
  --output dist/Ribbon-Ciano-8.0.0.tthm --title-id FLOWCYAN \
  --title "Ribbon Ciano" --version 8.0.0 --type theme
python3 kernel/tools/titan_pkg.py build --source themes/ribbon-twilight \
  --output dist/Ribbon-Crepusculo-8.0.0.tthm --title-id FLOWTWILIGHT \
  --title "Ribbon Crepusculo" --version 8.0.0 --type theme
python3 kernel/tools/titan_pkg.py build --source themes/ribbon-scarlet \
  --output dist/Ribbon-Scarlet-8.0.0.tthm --title-id SCARLETFLOW \
  --title "Ribbon Scarlet" --version 8.0.0 --type theme
python3 kernel/tools/titan_pkg.py build --source themes/ribbon-wine \
  --output dist/Ribbon-Vinho-8.0.0.tthm --title-id FLOWWINE \
  --title "Ribbon Vinho" --version 8.0.0 --type theme
for package in dist/Ribbon-*.tthm; do
  python3 kernel/tools/titan_pkg.py verify "$package"
done
```

## Ribbon installation

Copy the four `.tthm` files to the USB directory:

```text
mass:/TITAN/packages/
```

Inside Project Titan, open `★ Debug Settings > Package Installer`, install each
theme, then select it in the theme settings. The installed content is published
transactionally under `mass:/TITAN/themes/<THEME-ID>/`.

## Physical acceptance gates

Before declaring a release, perform a clean PS2DEV link, boot in PCSX2, then
exercise real hardware for GS texture correctness, 32 kHz audio stability,
controller mapping, storage short reads, shell restoration, repeated launch/
exit, all four Ribbon themes and a complete GoldenEye mission.
