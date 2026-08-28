# RC4 validation — complete build from an extracted source ZIP

## Observed failure

The RC3 source package intentionally omitted `.git`, but `scripts/build_all.sh`
unconditionally executed:

```sh
git -C "$ROOT" archive --format=tar HEAD | tar -C "$work" -xf -
```

With no repository database, `git archive` produced no tar stream. The receiving
`tar` then reported `This does not look like a tar archive` and stopped before
the MIPS compilation began.

The USB installer error was independent: the requested target directory was not
mounted. `install_to_usb.sh` correctly refused to install into a normal/missing
directory.

## RC4 correction

- Detects Git mode only when the project directory itself is the exact Git root.
- Uses `git archive HEAD` for a real project checkout.
- Uses a filtered tar snapshot for an extracted release ZIP.
- Excludes `build`, `dist`, generated production headers, caches and stale host
  objects from the archive-mode snapshot.
- Regenerates bootloader and kernel production headers before compilation.
- Supplies deterministic archive-mode version metadata to Neutrino and Titan UL
  Engine builds without nested Git repositories.
- Supports deterministic source repackaging in both Git and ZIP modes.
- Suppresses the harmless `git ls-files` diagnostic in the hardening host test.

## Validation performed in this environment

- `bash -n scripts/*.sh`: PASS
- complete `scripts/validate_source.sh`: PASS
- filtered source snapshot followed by complete host validation: PASS
- two independent archive-mode source packages: byte-identical
- RC3 versus RC4 asset hashes:
  - kernel assets: 236 files unchanged
  - bootloader assets: 3 files unchanged
  - runtime audio: 7 files unchanged

PS2DEV/PS2SDK is not installed in this environment, so this report does not
claim a MIPS build, PCSX2 execution or physical PS2 validation.
