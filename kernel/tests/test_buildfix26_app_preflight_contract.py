#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
app = (root / 'docs/history/buildfix/BUILDFIX26_APP_AUDIT.md').read_text()
mat = (root / 'docs/history/buildfix/BUILDFIX26_NATIVE_PORT_MATRIX.md').read_text()
plan = (root / 'docs/history/buildfix/BUILDFIX27_PRE_FLIGHT.md').read_text()
wle = (root / 'apps/wlaunchelf/README.md').read_text()
nes = (root / 'apps/project-nes/README.md').read_text()
browser = (root / 'apps/internet-browser/README.md').read_text()
api = (root / 'apps/wlaunchelf/include/titan_app_sdk.h').read_text()
net = (root / 'kernel/src/services/titan_browser_net.c').read_text()
http = (root / 'kernel/src/services/titan_http_client.c').read_text()
network_client = (root / 'kernel/src/services/titan_network_client.c').read_text()
assert 'A: TITAN_NATIVE (kernel system service)' in app
assert 'A: TITAN_NATIVE (partial feature parity)' in app
assert 'A: TITAN_NATIVE (emulation core incomplete)' in app
assert 'C: EXTERNAL_ELF' in app
assert 'SAS compliant/compatible' in app and 'orthogonal' in app
assert '.tmod, not an externally executed' in wle or '`.tmod`, not an externally executed' in wle
assert 'Titan App nativo' in nes
assert '@system-browser' in browser
for token in ('storage_list', 'storage_mkdir', 'storage_rename', 'launch_elf'):
    assert token in api
# BuildFix26 originally proved that Browser had working PS2IP transport.
# BuildFix27 intentionally moved that ownership behind Titan Network Client/HTTP.
# Preserve the historical capability requirement without forcing a regression
# back to browser-private sockets.
legacy_direct = all(token in net for token in ('gethostbyname', 'socket(', 'connect('))
shared_transport = (
    all(token in http for token in ('gethostbyname', 'socket(', 'connect('))
    and 'TitanNetworkClient' in network_client
    and all(token not in net for token in ('gethostbyname', 'socket(', 'connect('))
)
assert legacy_direct or shared_transport
assert 'FCEUmm-PS2' in plan and 'libretro FCEUmm' in plan
assert 'Do not blindly vendor' in plan
assert 'GPL-2.0' in plan
assert 'A TITAN_NATIVE system service' in mat
print('buildfix26-app-preflight-contract: OK')
