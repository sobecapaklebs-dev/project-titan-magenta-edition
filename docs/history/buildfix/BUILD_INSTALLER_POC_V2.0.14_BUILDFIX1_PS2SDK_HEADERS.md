# Titan Installer v2.0.14 BuildFix1 — PS2SDK header/domain fix

## Hardware compiler failure fixed

The real PS2DEV build reported `FIO_O_*` undeclared inside
`install_iop_open_flags_from_posix()`. Those constants are declared by
PS2SDK `io_common.h`; `fileXio_rpc.h` includes `fileXio.h` but does not make
`io_common.h` part of its public include chain. BuildFix1 includes it
explicitly.

## Flag-domain audit

The v2.0.14 code also translated the mode passed to `mcOpen()` into the
FileXio `FIO_O_*` namespace. PS2SDK documents `mcOpen()` as accepting the
normal `O_RDWR`, `O_CREAT`, etc. mode. BuildFix1 therefore keeps translation
only for `fileXioOpen()` and passes the original `O_*` mode to libmc.

No TINS, UI, renderer, theme, transaction format, or installer flow changes
were made.

## Build

```bash
make -C kernel clean
make -C kernel installer
```

Expected root artifact: `titan-installer.elf`.
