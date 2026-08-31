
## CP15 network ownership
- Native `.tmod` applications receive an append-only asynchronous GET API; no PS2IP/DNS/socket/TLS/thread handle crosses the ABI.
- One app request at a time is serviced through `TitanNetworkClient` owner `NATIVE_APP`, sharing the same single-flight arbitration used by Browser and Asset Service.
- Response memory is transient and bounded by the app-requested size with a hard 64 KiB service ceiling; it is not charged as app heap because the app cannot retain or address it directly.
- Runtime close/abort invalidates the request generation and requests cancellation before module/media/heap reclaim. A draining worker holds only service-owned memory and prevents buffer reuse until it exits.
