# Security Policy

MGB64 is experimental preservation software, but security and repository hygiene
still matter.

## Supported versions

Only the current `main` branch is supported. There are no packaged stable
release lines yet.

## Reporting vulnerabilities

For crashes, unsafe file handling, dependency issues, or accidental repository
contamination risks, use GitHub private vulnerability reporting if it is enabled
for the repository. If private reporting is not available, open a minimal issue
that asks for maintainer contact and does not include exploit details, ROMs,
save files, or copyrighted game assets.

Please include:

- the affected commit;
- your platform and build target;
- concise reproduction steps;
- logs or stack traces as text.

Do not attach ROMs, extracted assets, screenshots containing game imagery, or
large binary samples. If a report involves asset handling, describe the file type
and path shape instead of sharing the data.

## Scope

In scope:

- memory-safety bugs in the native port;
- unsafe path handling in ROM loading, extraction, or save paths;
- build/CI issues that could publish ROM-derived data;
- bundled third-party dependency vulnerabilities.

Out of scope:

- cheats, gameplay exploits, or parity differences with original hardware;
- issues that require modified ROMs or copyrighted assets to demonstrate;
- denial-of-service reports that only exhaust local CPU/GPU during normal play.
