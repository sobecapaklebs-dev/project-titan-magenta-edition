# BuildFix30 Acceptance Corrections — CP13

The first BF30 FINAL candidate was rejected after a source-level acceptance audit. CP13 corrects the discovered implementation gaps without moving scope to BF31.

## Installer language
The installer language selector now enables Portuguese (Brazil), English and Spanish. Selection is applied to the installer UI translation layer; English is no longer a disabled POC entry.

## Autonomous exploit compatibility
`EXPLOIT + TITAN_TINS_FLAG_AUTONOMOUS` is necessary but no longer sufficient. The installer additionally requires a known model ID, known console family, valid ROMVER region marker and a ROM generation consistent with the detected hardware family. Unknown/mismatched environments fail closed. Memory-card availability and the existing transactional backup/recovery path remain required downstream.

## Disc metadata
Disc identification now performs an optional local metadata-cache read on the serialized storage worker, never on the frame path. `mass:/TITAN/cache/disc/<TITLEID>.cfg` may provide title/icon/cover/background and an IGR profile hint. Missing/invalid metadata keeps the existing fallback and never blocks launch.

## Disc Smart IGR
Physical-disc compatibility profiles are explicit and capability-gated: 100 RETURN_ONLY, 101 MINIMAL, 102 FULL. Unknown discs remain UNKNOWN + RETURN_ONLY. A profile that requests capabilities unavailable from the resident backend is rejected rather than promoted unsafely.

No hardware validation is inferred by these host/source corrections.
