# BuildFix27 Checkpoint CP16
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP15
BLOCK: Network Client executable arbitration + App SDK ABI compatibility fixtures

- Added executable host fixture for Network Client success/timeout/cancel/nested busy behavior.
- Nested acquisition while a request owns the client fails closed and increments rejected-busy telemetry.
- Timeout and cancellation counters are independently verified.
- Added C ABI fixture reconstructing the exact legacy TitanAppApiV1 tail through text_input_poll.
- `offsetof(TitanAppApiV1, network_get_begin) == sizeof(legacy_tail)` passes.
- CP15 async app network contract, CP14 Browser ownership contract, Browser system contract, Project NES and wLaunchELF host suites remain green.

Material persistence:
- Parent: CP15 material chain.
- Source delta: BUILDFIX27_CP16_INCREMENTAL.tar.gz.
- Prior source/checkpoint archives remain untouched.

NEXT: CP17 resume wLaunchELF parity work now that reusable network service exists; implement Titan-rendered text-file viewing/editing first, then network/FTP only on a proven bounded stream API rather than bypassing Titan ownership.
