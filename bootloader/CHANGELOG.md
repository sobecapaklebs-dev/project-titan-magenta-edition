# Titan Bootloader changelog

## 0.1.4-rc3

- Targets Project Titan Magenta Edition kernel `0.3.90-rc3` and Titan UI `0.9.23`.
- Keeps the validated RC2 boot path unchanged.

## 0.1.4-rc2

- Targets Project Titan Magenta Edition kernel `0.3.90-rc2`.
- Prioritizes the canonical root `kernel.elf` path.
- Uses the shared strict ELF validator.
- Writes stage2 logs with exact partial-write handling.
- Keeps generated screen/stage2 headers out of the source package.

## 0.1.3-rc1

- Confirmed boot chain used by the consolidated RC1 baseline.
