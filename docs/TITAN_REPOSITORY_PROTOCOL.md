# Titan Repository Protocol v1

Titan repositories are static-host friendly UTF-8 text files. No JavaScript or server-side API is required.

The first line is `TITAN-REPOSITORY/1`. Repository metadata uses `id=` and `name=`. Each package is a `[package PACKAGE_ID]` section with bounded `key=value` fields.

Required package fields: `title`, `version`, `platform=ps2`, `architecture=mips-r5900`, absolute HTTP/HTTPS `url`, 64-hex `sha256`, and positive `size`. Optional fields: `description`, `icon`, `background`, `api`, `dependencies`.

Package IDs are stable logical identities. URLs may point at any static host. SHA-256 and size are authoritative download integrity metadata; the Download Service never publishes a completed file until both integrity and Titan package validation pass.

Author validation:

```sh
python3 kernel/tools/titan_repository.py path/to/catalog.trepo
```

The target parser is fixed-capacity (48 packages per catalog in v1), performs no heap allocation and rejects malformed/duplicate/incompatible entries.
