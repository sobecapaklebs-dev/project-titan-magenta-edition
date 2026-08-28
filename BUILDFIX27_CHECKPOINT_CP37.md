# BuildFix27 Checkpoint CP37
STATUS: HOST_VALIDATED / TARGET_PROVIDER_BLOCKED
PREVIOUS_VALID: CP36
BLOCK: wLaunchELF PFS mount/status/unmount UX

- R1 at device root uses the optional CP36 PFS ABI only when the append-only tail is present.
- Mount source is collected through Titan Text Input; default hint is hdd0:+.
- wLaunchELF requests READ|WRITE capabilities through Titan; it never calls FileXio/mount primitives.
- Successful mounts appear through storage_list_volumes; selecting pfsN:/ + R1 requests session-owned unmount.
- Provider unavailable/runtime without CP36 tail fails closed with explicit status.
- Host lifecycle fixture proves mount -> pfs0 volume -> unmount -> volume removal.

NEXT: CP38 target PFS provider adapter / fail-closed integration.
