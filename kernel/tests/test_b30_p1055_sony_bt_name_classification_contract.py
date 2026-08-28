#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
ds = (root / "modules/titan-ul-engine/modules/pademu/ds34bt.c").read_text()

def need(v, m):
    assert v, m

need("P10.5.5: Sony Bluetooth names are firmware-dependent." in ds,
     "P10.5.5 helper missing")
for name in (
    '"Wireless Controller"',
    '"DualSense Wireless Controller"',
    '"DualSense Edge Wireless Controller"',
    '"DUALSHOCK 4 Wireless Controller"',
):
    need(name in ds, f"accepted Sony BT name missing: {name}")

need("wireless_is_sony_controller_name(wa->name)" in ds,
     "discovery/UI Sony classification not migrated")
need("wireless_is_sony_controller_name((const char *)(hci_buf + 9))" in ds,
     "direct-pad remote-name Sony classification not migrated")
need('memcmp(hci_buf + 9, "Wireless Controller", 19) == 0' not in ds,
     "legacy exact-name direct-pad classifier still active")
need("TITAN_WIRELESS_BA(19)" in ds,
     "Sony classification breadcrumb missing")
need("0x43414348u" in ds,
     "authentication cache-order recovery missing")
need("TITAN_WIRELESS_BA(13)" in ds,
     "P10.5.4 L2CAP TX evidence regressed")
need("g_wireless_l2cap_watch_pad" in ds,
     "P10.5.4 bounded L2CAP watchdog regressed")
need("hci_buf[2] == 0x0B && g_wireless_direct_pad >= 0" in ds,
     "P10.5.4 duplicate ACL defense regressed")
need("iomanX_open" not in ds and "iomanX_write" not in ds,
     "PADEMU regained direct filesystem I/O")

print("b30-p1055-sony-bt-name-classification: PASS modern Sony names + auth-order recovery + P10.5.4 L2CAP handoff preserved")
