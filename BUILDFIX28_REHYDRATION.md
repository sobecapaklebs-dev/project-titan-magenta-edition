# BuildFix28 Final Recovery

The primary preservation artifact for the final BuildFix28 delivery is:

`Project-Titan-BuildFix28-FINAL-SOURCE.tar.gz`

Validate it with its companion SHA-256 / `SHA256SUMS`, extract it to an empty directory, then verify `BUILDFIX28_FINAL_MANIFEST.txt`.

The historical BF27 + BF28 CP00..CP17 chain is retained under `INTERMEDIATE/` only as checkpoint provenance and development recovery. It is not required to compile, audit, or recover the final BF28 once the full final archive is successfully persisted and the FINAL independence gate passes.

Target build, PCSX2 and physical hardware validation remain external and are not asserted by recovery.
