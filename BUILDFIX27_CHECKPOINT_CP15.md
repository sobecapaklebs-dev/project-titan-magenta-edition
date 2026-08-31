# BuildFix27 Checkpoint CP15
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP14
BLOCK: Public native-app Network Client ABI + lifecycle containment

- App ABI v1 remains append-only; network functions are appended after text_input_poll.
- Added asynchronous network_get_begin/status/read/cancel; apps never receive PS2IP, socket, DNS, TLS or thread ownership.
- Requests route through TitanNetworkClient owner NATIVE_APP and share single-flight arbitration with Browser/Asset Service.
- Each app request chooses a bounded response size with a hard 64 KiB ceiling and an explicit deadline capped at 120 s.
- Response storage is transient service memory allocated per request, not permanent 64 KiB BSS and not app-addressable heap.
- Runtime close/abort abandons the network generation before module/media/heap reclaim.
- A draining old worker owns only service memory; worker_active prevents buffer reuse until the old request exits.
- Canonical SDK header and Project NES/wLaunchELF SDK copies are byte-identical.

Contracts:
- buildfix27-app-network-api-contract: PASS
- buildfix27-network-client-browser-contract: PASS
- Titan App runtime ET_REL/R5900 ASan/UBSan harness: PASS
- system-browser-contract: PASS
- asset-download-ux-bf13-contract: PASS 20/20
- Project NES CP10R/CP11/CP12/CP13 + host suite: PASS
- wLaunchELF host lifecycle: PASS
- strict host compile titan_network_client.c/titan_app_network.c: PASS

Material persistence:
- Parent: CP14 material chain.
- Source delta: BUILDFIX27_CP15_INCREMENTAL.tar.gz.
- Full-source upload is intentionally not retried while Library quota rejects large archives; no baseline or prior checkpoint is deleted.

NEXT: CP16 network cancellation/timeout/HTTP result lifecycle fixtures + wLaunchELF network consumer readiness; then use the public API for FTP features without direct PS2IP ownership.
