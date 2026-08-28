# Project Titan system update packages (`.pup`)

BuildFix20 introduces TPUP v1, the USB system-update container used by
**Settings > System > Atualizacao do software do sistema**.

## Discovery

The updater scans:

```text
mass:/TITAN/update/*.pup
mass:/TITAN/packages/*.pup
```

Only packages whose `build` is greater than the running
`TITAN_SYSTEM_UPDATE_CURRENT_BUILD` are offered. Equal or lower builds are
rejected; the update path does not provide downgrade.

## Safety model

A TPUP package contains 1..16 files. Targets are declarative and are restricted
to:

```text
kernel.elf
kernel
TITAN/...
bootloader/...
```

Absolute paths, `..`, device prefixes and backslashes are rejected. The file
table and every payload entry carry CRC32 checksums. Extraction occurs in a
private staging directory before publication. Multi-file publication retains
transaction backups only until every target has been swapped; if any swap
fails, already-published files are restored. On success those temporary backups
are deleted, intentionally leaving no persistent downgrade copy.

## Build a PUP

After compiling the next kernel on the PS2DEV host:

```text
python3 kernel/tools/titan_pup.py build \
  --output Project-Titan-BuildFix21.pup \
  --build 21 \
  --version 0.3.90-buildfix21 \
  --file kernel.elf=kernel.elf
```

Additional updated files may be appended with more `--file target=local-path`
arguments.

Verify before copying it to USB:

```text
python3 kernel/tools/titan_pup.py verify Project-Titan-BuildFix21.pup
```

BuildFix20 itself must be installed once by replacing `kernel.elf`, because an
older kernel cannot understand TPUP. BuildFix21 and later can then be tested via
the updater.
