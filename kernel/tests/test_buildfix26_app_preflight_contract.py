#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
wle = (root / 'apps/wlaunchelf/README.md').read_text()
nes = (root / 'apps/project-nes/README.md').read_text()
browser = (root / 'apps/internet-browser/README.md').read_text()
api = (root / 'apps/wlaunchelf/include/titan_app_sdk.h').read_text()
net = (root / 'kernel/src/services/titan_browser_net.c').read_text()
http = (root / 'kernel/src/services/titan_http_client.c').read_text()
network_client = (root / 'kernel/src/services/titan_network_client.c').read_text()
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
print('buildfix26-app-preflight-contract: OK')
