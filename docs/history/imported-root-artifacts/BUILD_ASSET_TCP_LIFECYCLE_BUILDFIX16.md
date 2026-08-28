# Project Titan RT1 BuildFix16 — Asset TCP Lifecycle & Session Drain

BF16 addresses the long-session failure pattern observed on real PS2 hardware after BF15 successfully published 15 assets.

## Hardware evidence
- The BF15 flight recorder no longer showed the former TLS-engine/protocol/storage corruption pattern.
- After sustained asset activity, DNS failures began taking ~13 seconds and cached-host TCP connects took ~30 seconds before failing.
- This is consistent with excessive short-lived socket/PCB churn on the small PS2IP/lwIP stack.
- A cancelled session still emitted `ASSET_SESSION_END` shortly before the final in-flight TCP operation exited.

## Transport changes
- The HTTP client now owns a two-slot persistent HTTP/1.1 keep-alive pool. The two slots cover the two effective hosts used by the provider set (`cdn.jsdelivr.net` and `raw.githubusercontent.com`).
- Complete framed responses keep their TLS/TCP connection alive for later assets.
- 404/410/error response bodies are consumed only when framing is bounded, allowing the connection to be reused without treating HTML bodies as game assets.
- A stale pooled connection is retried once on a fresh connection before the provider is failed.
- DNS caching is now four-entry/LRU instead of a single-host cache, eliminating repeated DNS allocation/query churn when switching providers.
- Cached DNS entries self-invalidate after two consecutive TCP-connect failures.
- New diagnostics distinguish socket creation failure, connect failure, connection reuse and pool close.

## Session ownership
- Manual cancellation is now a drain barrier.
- The network worker no longer clears the session cancel token.
- `ASSET_SESSION_END` is emitted only after network, download staging and storage ownership are drained.
- Persistent HTTP connections are closed and DNS cache is reset at safe manual-session boundaries.

## Preservation
- Asset semantics remain exactly ICON and BACKGROUND.
- BF15 payload validation/decoder/storage ownership remains intact.
- Network Core, AR9271 IOP path and frozen bootloader are unchanged.
