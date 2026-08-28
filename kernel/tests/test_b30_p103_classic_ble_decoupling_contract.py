#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
src = (ROOT / "modules/titan-ul-engine/modules/pademu/ds34bt.c").read_text()

assert "g_xbox_ble_hci_capable" in src
assert "g_xbox_ble_resume_pending" in src
assert "Never gate Classic Inquiry on LE entropy" in src
assert "User-visible discovery gets priority" in src

read_idx = src.index("else if (hci_buf[3] == 0x09 && hci_buf[4] == (0x04<<2)")
window = src[read_idx:read_idx+2500]
assert "if (g_wireless_scan_requested)" in window
assert "g_wireless_hci_startup_ready = 1;" in window
assert "(void)wireless_start_classic_scan_sequence();" in window
assert window.index("(void)wireless_start_classic_scan_sequence();") < window.index("xbox_ble_request_rand();")

rand_idx = src.index("if (hci_buf[3] == 0x18 && hci_buf[4] == 0x20)")
rand_window = src[rand_idx:rand_idx+3500]
assert "hci_buf[1] >= 12" in rand_window
assert "g_xbox_ble_hci_capable = -1;" in rand_window
assert "g_wireless_hci_startup_ready = 1;" in rand_window
assert "0x00002018u" in rand_window
assert "TITAN_WIRELESS_BA(6)" in rand_window
assert "if (g_wireless_scan_requested)" in rand_window
assert "(void)wireless_start_classic_scan_sequence();" in rand_window

resolve_idx = src.index("Classic discovery is complete. Probe/resume the Xbox BLE lane")
resolve_window = src[resolve_idx:resolve_idx+1800]
assert "g_xbox_ble_hci_capable < 0" in resolve_window
assert "xbox_ble_random_count >= 16" in resolve_window
assert "g_xbox_ble_resume_pending = 1;" in resolve_window
assert "xbox_ble_request_rand();" in resolve_window

assert "hci_cmd_buf[3] = 0x33;" in src
assert "hci_cmd_buf[4] = 0x8B;" in src
assert "hci_cmd_buf[5] = 0x9E;" in src
assert "hci_cmd_buf[6] = 0x04;" in src

print("b30-p103-classic-ble-decoupling: PASS Classic Inquiry independent from LE Rand; BLE capability fail-closed")
