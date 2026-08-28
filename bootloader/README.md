# Titan Bootloader 0.1.14-rc14

The Titan Bootloader starts the Project Titan Magenta Edition kernel and embeds
a low-memory stage2 used for validated external ELF handoff.

## Canonical kernel search order

1. `mass:/kernel.elf`
2. `mass0:/kernel.elf`
3. `mass1:/kernel.elf`
4. controlled legacy sibling candidates for migration/recovery

The active installer never writes `mass:/TITAN/kernel.elf`. Every candidate is
validated by the shared strict MIPS ELF validator before execution.

## Build

```sh
make -C stage2 clean all
make clean all
make test-host
```

Production generated headers are recreated from raw assets/stage2 and are not
tracked in source packages.
