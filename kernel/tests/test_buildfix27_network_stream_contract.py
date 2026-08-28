#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
h=(root/'kernel/src/services/titan_network_stream.h').read_text(errors='replace')
c=(root/'kernel/src/services/titan_network_stream.c').read_text(errors='replace')
mk=(root/'kernel/Makefile').read_text(errors='replace')
def req(x,m):
    if not x: raise SystemExit('buildfix27-network-stream-contract: FAIL: '+m)
req('TitanNetworkStreamHandle' in h and 'TitanNetworkStream_Listen' in h and 'TitanNetworkStream_Accept' in h,'opaque listener/accept API missing')
req('TitanNetworkStream_Read' in h and 'TitanNetworkStream_Write' in h and 'TitanNetworkStream_CancelOwner' in h,'bounded I/O/cancel API missing')
req('TITAN_NETWORK_STREAM_CONNECTION_SLOTS 4u' in c and 'TITAN_NETWORK_STREAM_LISTENER_SLOTS 2u' in c,'fixed capacity policy missing')
req('TitanNetworkStream_ClampDeadline' in c and 'TITAN_NETWORK_STREAM_MAX_DEADLINE_MS 30000u' in c,'deadline bound missing')
req('socket(' in c and 'bind(' in c and 'listen(' in c and 'accept(' in c and 'recv(' in c and 'send(' in c,'Titan-owned PS2IP backend incomplete')
req('src/services/titan_network_stream.o' in mk,'network stream service not linked into kernel')
print('buildfix27-network-stream-contract: OK opaque bounded Titan-owned TCP stream service')
