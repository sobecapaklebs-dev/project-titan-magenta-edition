# RC8 Debug Settings and Package Installer build note

## Release metadata

- Kernel: `0.3.90-rc8`
- Titan UI: `0.9.28`
- Bootloader: `0.1.8-rc8`

## Implemented scope

RC8 adds `★debug settings`, the transparent white supplied icon, U+2605 in the
final unused font-atlas cell, matching `★ APP_HOME`/`★ FTP_HOME` titles and
eight functional debug actions. TPKG v1 packages are validated, extracted
incrementally and published transactionally to `mass:/TITAN/apps/TITLEID`.
Their valid manifests become launchable Home tiles.

The package contract, builder command and USB paths are documented in
`docs/TITAN_PACKAGE_FORMAT.md`.

## Validation boundary

The host tests cover the menu/source contracts, atlas integrity, package
builder/verifier, CRC failures, unsafe paths, cancellation, transactional
cleanup, manifest discovery and Home application publication. PS2DEV and
PS2SDK were not available in the preparation environment, so no PS2/MIPS
binary or physical-console validation is claimed for RC8.

## Clean build commands

Run these from the extracted project directory in a configured PS2DEV shell:

```text
make -C kernel clean
make -C kernel clean-generated
find kernel -type f -exec touch {} +
make -C kernel -j4
```

After the complete project runtime has been built, use the existing release
make targets appropriate to the local PS2DEV checkout. Keep the RC7 tree as a
backup until the resulting ELF has been exercised in PCSX2 and on hardware.
