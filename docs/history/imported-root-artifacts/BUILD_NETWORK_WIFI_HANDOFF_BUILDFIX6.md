# Project Titan Magenta Edition — RT1 BuildFix6 Wi-Fi Single-Owner Handoff

## Hardware trigger

On physical PS2, TT1426 completed WPA2 authentication and reported the Wi-Fi
connection as successful, then the Titan shell froze and the external-HDD
activity LED stopped. The failure occurred at the transition from the
standalone TT1426 online-proof path to NetMan/PS2IP, not during WPA2 itself.

## Corrections

- `TITAN_AR9271_SCAN_RPC_NETMAN_ACTIVE` no longer calls
  `NetManToggleNetIFLinkState()` from the SIF RPC server thread. The RPC only
  records the requested ownership state and wakes a dedicated IOP control
  worker.
- The AR9271 NetMan interface now publishes the real radio/station MAC after
  radio initialization instead of retaining the PS2 console-MAC fallback.
- Post-handoff protected IPv4/ARP frames have exactly one owner: once NetMan is
  active, frames are delivered to NetMan/PS2IP and are not also fed to the
  TT1426 DHCP/ARP/ICMP proof parser.
- New/retry/disconnect paths revoke bridge ownership before starting another
  standalone proof run.
- EE waits 30 frames after TT1426 ONLINE_PASS before requesting the handoff.
- EE does not restart PS2IP DHCP in the same call that requests IOP bridge
  activation. DHCP begins only after NetMan reports WFI link-up.
- Titan Asset Service waits 180 ONLINE frames before the first direct-Internet
  asset request, preventing TLS/image work from bursting on the handoff frame.

## Preserved behavior

- Official bootloader BF6 is unchanged.
- BDM/USB mass ownership/order is unchanged.
- TT1426 WPA2/DHCP/ARP/PING proof path remains intact before handoff.
- PS4-style Wi-Fi scan/settings, RJ45 support, Flow theme work, and direct
  Internet Asset Service remain present.

## Host validation

- `network-wifi-handoff-bf6: PASS`
- `AR9271 resident full translation-unit syntax guard: PASS`
- `Titan Network EE service syntax guard: PASS`
- `Titan Asset Service EE syntax guard: PASS`
- `network-runtime-compact BF2 contract: PASS`
- `network PS4-style auto-scan UI BF3 contract: PASS`
- `asset-service-contract: PASS`
- `network-theme-integration contract: PASS`
- `boot-usb-bdm-bf6-wle-order: OK`
- `boot-usb-handoff-fix: OK`

## Hardware validation boundary

This is not hardware-PASS until tested on the physical PS2. The decisive test
is whether the shell remains alive when TT1426 reaches ONLINE_PASS and the WFI
link is handed to NetMan/PS2IP while the external HDD remains mounted/alive.
