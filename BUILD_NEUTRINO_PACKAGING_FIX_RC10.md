# Project Titan Magenta Edition 0.3.90-rc10

- Kernel: `0.3.90-rc10`
- Titan UI: `0.9.30`
- Bootloader metadata: `0.1.10-rc10`

## Corrected failure

RC9 collected the Titan Neutrino Engine IRX files with `make copy` and then
invoked the EE loader `clean all` target. The loader clean rule removes the
generated `ee/loader/modules` directory, so the complete engine builds
finished but runtime publication failed at the final `cp -R`.

RC10 rebuilds the loader first and collects the IRX module payload last. The
packager now stops with a component-specific diagnostic if `neutrino.elf`,
`config`, `modules` or the expected IRX payload is absent.

No renderer, storage, profile, DEX, catalogue, application, package-installer,
engine-runtime or visual asset behavior was changed.

## Validation boundary

The complete host validation suite and extracted-source contracts must pass.
A complete PS2DEV build and physical PS2 validation remain required before
release approval.
