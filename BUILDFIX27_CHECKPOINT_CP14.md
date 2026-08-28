# BuildFix27 Checkpoint CP14
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP13
BLOCK: Browser + reusable Titan Network Client ownership boundary

- Added owner-aware single-flight TitanNetworkClient above the existing bounded HTTP/HTTPS transport.
- Browser and Asset Service now acquire explicit Network Client ownership.
- Browser no longer contains gethostbyname/socket/connect/send/recv/lwip_close or PS2IP/netdb includes.
- Browser cancellation is routed into the shared transport and every request declares a 60 s deadline.
- Lower transport retains bounded select() waits, HTTPS certificate-chain and hostname validation.
- Browser download publication remains TitanStorageIo + AtomicReplace; no browser-private FileXio ownership.
- AR9271/Ethernet/DHCP/DNS device lifecycle remains Titan Network Core-owned.
- CP10R through CP13 Project NES and wLaunchELF regressions remain green.

Contracts:
- buildfix27-network-client-browser-contract: PASS
- system-browser-contract: PASS
- asset-download-ux-bf13-contract: PASS 20/20
- Project NES CP10R/CP11/CP12/CP13 contracts: PASS
- Project NES host suite: PASS
- wLaunchELF host lifecycle: PASS
- strict host compile of new Network Client + Browser net: PASS

NEXT: CP15 public native-app Network Client ABI/service bridge + cancellation/lifecycle containment, without exposing PS2IP or device-specific backends.
