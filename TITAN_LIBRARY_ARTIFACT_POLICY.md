# Titan Build Artifact Organization Policy

Beginning with BuildFix28 closure and mandatory for BuildFix29+:

```text
/Project Titan/BuildFixXX/
    INTERMEDIATE/
    FINAL/
```

`INTERMEDIATE/` contains material checkpoints, deltas, checkpoint hashes/manifests/validation and temporary recovery evidence. It is never auto-deleted.

`FINAL/` contains the final full source archive, its SHA-256/SHA256SUMS, final manifest, validation/changelog/status/build instructions/provenance and final recovery/hardware/compatibility documents.

A build may mark `INTERMEDIATE_SAFE_TO_DELETE=1` only after simulating complete removal of `INTERMEDIATE/` and proving that `FINAL/` alone remains identifiable, integrity-verifiable, buildable with documented external dependencies, auditable and recoverable.
