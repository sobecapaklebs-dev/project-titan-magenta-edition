#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
ds = (root / 'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()

def need(v, m):
    assert v, m

# P10.5.6 functionality.
# Compile-order guard: Feature05 helper is intentionally above HID_command's
# implementation, so a matching forward declaration must precede the helper.
_proto = 'static int HID_command(u16 handle, u16 scid, u8 *data, u8 length, int pad);'
_helper = 'static int hid_request_sony_bt_feature05(int pad)'
_def = 'static int HID_command(u16 handle, u16 scid, u8 *data, u8 length, int pad)\n{'
need(ds.find(_proto) >= 0 and ds.find(_proto) < ds.find(_helper) < ds.find(_def),
     'HID_command forward declaration missing or ordered after Feature05 helper')

need('P10.5.6 Sony basic BT report 0x01: 10-byte gamepad payload.' in ds,
     'Sony basic-report parser missing')
need('data[9] == 0x01 && pad->sony_generation_pending' in ds,
     'Sony basic report is not gated by generation_pending')
need('if (bytes < 19)' in ds and 'mips_memcpy(&basic, &data[9], 10)' in ds,
     '19-byte/10-byte hardware bounds missing')
need('translate_pad_ds4(&basic, &pad->ds2, 0)' in ds,
     'Sony basic report not promoted to DS2-compatible state')
need('pad->telemetry_packets++' in ds,
     'basic Sony input cannot become telemetry input_valid')
need('TITAN_WIRELESS_BA(20)' in ds,
     'basic HID acceptance breadcrumb missing')

need('static int hid_request_sony_bt_feature05(int pad)' in ds,
     'Feature 0x05 helper missing')
need('request[0] = HID_THDR_GET_REPORT_FEATURE' in ds and
     'request[1] = 0x05' in ds,
     'Sony Feature Report 0x05 request missing')
need('g_p1056_sony_feature05_requested[pad_idx] = 1' in ds,
     'Feature 0x05 request is not one-shot')
need('g_p1056_sony_feature05_requested[pad] = 0' in ds,
     'Feature 0x05 one-shot is not reset per connection')
need('TITAN_WIRELESS_BA(21)' in ds,
     'Feature 0x05 request breadcrumb missing')

need('P10.5.6: successful A1 HID traffic is high-rate; sample it.' in ds,
     'L2CAP RX QoS sampling missing')
need('g_p1056_l2cap_hid_rx_packets <= 4u' in ds and
     '(g_p1056_l2cap_hid_rx_packets & 255u) == 0u' in ds,
     'L2CAP HID trace is not first-4/1-in-256 sampled')
need('resultCode != USB_RC_OK' in ds and
     'l2cap_buf[8] != HID_THDR_DATA_INPUT' in ds,
     'L2CAP errors/signalling are not retained')

# Preserve the hardware-proven pairing chain and ownership fixes.
for marker in (
    'P10.5.5: Sony Bluetooth names are firmware-dependent.',
    'wireless_is_sony_controller_name',
    'TITAN_WIRELESS_BA(19)',
    'TITAN_WIRELESS_BA(13)',
    'g_wireless_l2cap_watch_pad',
    'hci_buf[2] == 0x0B && g_wireless_direct_pad >= 0',
):
    need(marker in ds, f'regressed required baseline marker: {marker}')
need('iomanX_open' not in ds and 'iomanX_write' not in ds,
     'PADEMU regained direct filesystem ownership')

print('b30-p1056-sony-basic-hid-trace-qos: PASS basic 0x01 input + one-shot feature05 + bounded HID RX trace; P10.5.5 preserved')
