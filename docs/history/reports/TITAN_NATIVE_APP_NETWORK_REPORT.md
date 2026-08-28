# Titan Native App Network Report — BuildFix27

## CP14 kernel consumers
Browser and Asset Service share `TitanNetworkClient`; Browser no longer contains direct PS2IP/lwIP DNS/socket calls. The lower Titan HTTP/HTTPS client remains the only transport implementation and retains bounded waits, deadlines, TLS certificate validation and hostname validation.

## CP15 public app ABI
`TitanAppApiV1` now exposes an append-only asynchronous bounded GET interface: begin, status, read and cancel. The API deliberately exposes no socket descriptor, DNS object, TLS context, device/backend handle or thread primitive.

A request is executed by a Titan-owned lazy worker through the same `TitanNetworkClient` arbitration used by system consumers. The response body is stored in transient Titan-owned memory capped at 64 KiB, then copied into app memory only when the app calls `network_get_read` after completion.

Lifecycle containment uses a request generation plus `worker_active`. App close/abort requests cancellation and invalidates the generation before the `.tmod` image is unloaded. A late worker cannot callback into app code or write app memory, and a new app session cannot reuse the response buffer until the old worker exits.

Target R5900 execution remains gated for CP21 because the PS2DEV target toolchain is not present in this environment.

## CP16 executable contracts
The shared client now has executable host coverage for successful completion, timeout accounting, cancellation accounting and nested/reentrant acquisition rejection. A separate C ABI fixture freezes the pre-network `TitanAppApiV1` tail and proves with `offsetof` that `network_get_begin` starts exactly at the legacy structure size; network status/read/cancel follow it monotonically.
