# DEX operation-status ribbon

The compact operation ribbon in the shell header is a general diagnostics surface,
not an asset-only widget. It is visible whenever **DEX mode** is enabled; it does
not depend on the optional large DEX telemetry panel. Normal users see no ribbon.

## API contract

Any feature with meaningful work stages can report its latest state through
`TitanDiagnostics_ReportOperation`:

```c
TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
    "PROBE", "SLUS_215.96", -1);
TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_INFO,
    "READ", "SLUS_215.96_COV.jpg", 65);
TitanDiagnostics_ReportOperation("ASSET", TITAN_DIAG_ERROR,
    "DECODE ERROR", "SLUS_215.96_COV.jpg", -1);
```

- `area` identifies the subsystem (`ASSET`, `LIBRARY`, `NETWORK`, and so on).
- `stage` is the current operation stage; `message` is its subject/result.
- Use `0..100` for a known percentage and `-1` when progress is not measurable.
- Publish meaningful transitions and update long operations as they make progress;
  do not publish from the frame-render hot path.
- Errors stay visible until another operation replaces them or the owner calls
  `TitanDiagnostics_ClearOperation(area)`. Normal status expires shortly after
  its last update, so abandoned progress does not remain on screen indefinitely.
- Call `TitanDiagnostics_GetOperationStatus` to read a thread-safe snapshot.
  Producers must use bounded strings and clear only their own `area`.

The current producer is the local game-art pipeline. It reports probing, finding,
reading, decoding, fallback, successful residency, and terminal decode failures.
