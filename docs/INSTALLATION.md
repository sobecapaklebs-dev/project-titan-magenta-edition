# Installation and USB Deployment

## Initial public release: Semi-autonomous / DEV1

The initial public release supports the **Semi-autonomous** installation path for consoles using a compatible Matrix/Modbo DEV1/R1 boot environment.

Canonical boot chain:

```text
POWER ON
  -> DEV1 / R1
  -> mcX:/BOOT/BOOT.ELF
  -> Titan Bootloader
  -> /kernel.elf on USB
  -> Project Titan
```

The bootloader filename is **`BOOT.ELF` in uppercase**. Do not rename it to `boot.elf`, `Boot.elf` or `titan.elf` on the Memory Card release path.

## Autonomous / Free McBoot status

The repository retains the integrated Free McBoot autonomous-install source for continued development.

It is **experimental and disabled from the initial public release media**. The first release must not present Autonomous as an installation option.

Autonomous installation can be enabled in a future release only after its physical compatibility matrix, MagicGate/KELF behavior, recovery/rollback and repeated cold-boot path have been validated on representative consoles.

## Development USB layout

A development USB commonly contains:

```text
USB/
├── kernel.elf
└── TITAN/
    ├── apps/
    ├── packages/
    ├── cache/
    └── ...
```

The kernel used by the current USB boot chain is deployed to the root as:

```text
/kernel.elf
```

## Safe kernel deployment

Example:

```bash
ROOT="$HOME/Projects/project-titan/bf30/targetfix8"
USB="/run/media/$USER/<USB-ID>"

cp -fv "$ROOT/kernel/kernel.elf" "$USB/kernel.elf"
sync

sha256sum "$ROOT/kernel/kernel.elf" "$USB/kernel.elf"
cmp -s "$ROOT/kernel/kernel.elf" "$USB/kernel.elf"
```

Do not remove the USB until pending writes have completed.

## Application packages

Development packages should be installed through Package Installer instead of manually copying their extracted payload into `TITAN/apps`.

Expected transaction:

```text
package
  -> validation
  -> .install-* staging
  -> file verification
  -> atomic/final commit
  -> installed application directory
```

A `.install-*` directory is staging state, not a successfully installed app.

## Release-media invariant

The initial release installer media must satisfy all of the following:

```text
TINS autonomous flag = 0
BOOT entry            = BOOT.ELF
embedded kernel       = the release-candidate kernel
```

Source and USB copies should be verified with SHA-256 and `cmp` before hardware testing.
