# Project Titan Magenta Edition 0.3.90-RC2 curated UI restoration

This hotfix restores the complete UI asset tree from commit `1d988cf`, the last
repository state before the RC2 source-hardening commit replaced the curated
icons, avatars, DEX artwork, boot mark, game placeholder, alert and power art
with procedural geometry.

## Protection added

- `make -C kernel system-assets` now verifies the curated asset hashes.
- Procedural fallback assets are generated only outside `kernel/assets/ui`.
- `generate_system_assets.py` refuses to overwrite the curated tree unless the
  destructive `--force-curated-overwrite` option is supplied explicitly.
- Host validation checks the curated manifest and independently smoke-tests the
  fallback generator in `/tmp`.

## Redistribution note

Some restored avatars and recognizable artwork were supplied for Project Titan
proof-of-concept/personal testing and may include third-party characters or
marks. Their restoration does not establish redistribution rights.
