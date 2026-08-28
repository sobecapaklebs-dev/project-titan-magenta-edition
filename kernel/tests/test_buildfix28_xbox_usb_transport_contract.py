#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
usb=(root/'modules/titan-ul-engine/modules/pademu/ds34usb.c').read_text()
h=(root/'modules/titan-ul-engine/modules/pademu/ds34usb.h').read_text()
common=(root/'modules/titan-ul-engine/include/ds34common.h').read_text()
mk=(root/'modules/titan-ul-engine/modules/pademu/Makefile').read_text()
required=['MICROSOFT_VID','XBOX_ONE_PID','XBOX_ONE_S_PID','XBOX_ELITE2_PID','XBOX_SERIES_PID']
for x in required: assert x in common, x
assert 'titan_xbox_usb_pid_supported' in usb
assert 'UsbOpenEndpointAligned' in usb and 'USB_ENDPOINT_XFER_INT' in usb
assert 'xbox_protocol.o' in mk
assert '#define XBOX_ONE  5' in h and '#define XBOX_SERIES 6' in h
assert 'usb_disconnect' in usb and 'usb_release(pad)' in usb
print('buildfix28-xbox-usb-transport-contract: OK')
