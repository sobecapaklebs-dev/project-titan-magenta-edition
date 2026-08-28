# Titan Installer v2.0.14 BuildFix2 — newlib header-order fix

## Compiler failure corrected

BuildFix1 explicitly included PS2SDK `io_common.h` to obtain the `FIO_O_*`
constants, but placed that include before the local `NEWLIB_PORT_AWARE` opt-in.
On a current PS2SDK/newlib build, `io_common.h` therefore stopped compilation
with:

- `Using fio/fileXio functions directly in the newlib port will lead to problems.`
- `Use posix function calls instead.`

BuildFix2 moves `io_common.h` into the same tightly-scoped
`NEWLIB_PORT_AWARE` window already used for `fileXio_rpc.h`. The macro is still
undefined immediately after those headers when this file defined it locally.

## Scope

No installer transaction, TINS, UI, renderer, theme, bootloader, storage policy,
FileXio flag translation, libmc behavior, or installation flow was changed.

## Build

```bash
make -C kernel clean
make -C kernel installer
```

Expected artifact: `titan-installer.elf`.
