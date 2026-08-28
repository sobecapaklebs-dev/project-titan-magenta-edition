#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(errors='replace')
mk=read('iop/titan_ar9271/Makefile')
scan=read('iop/titan_ar9271/ar9271_scanner.c')
h=read('iop/titan_ar9271/titan_postvif168_runtime.h')
c=read('iop/titan_ar9271/titan_postvif168_runtime.c')
net=read('src/services/titan_network.c')
objs=next(line for line in mk.splitlines() if line.startswith('IOP_OBJS ='))
assert 'titan_postvif168_runtime.o' in objs
assert 'titan_linux_coldstart_trace.o' not in objs
assert 'obj/titan_linux_coldstart_trace.o' in mk
assert '#include "titan_postvif168_runtime.h"' in scan
assert '#include "titan_linux_coldstart_trace.h"' not in scan
assert 'TITAN_POSTVIF168_RUNTIME_COUNT     168u' in h
assert 'titan_postvif168_runtime_blob_size = 3632u' in c
assert 'titan_postvif168_runtime_response_blob_size = 212u' in c
assert c.count('{ ') >= 168
active=scan.split('static int run_linux_postvif_168_live(void)',1)[1].split('#if 0 /* Standalone-only full Linux lockstep',1)[0]
for token in ('titan_postvif168_runtime_events','titan_postvif168_runtime_blob','titan_postvif168_runtime_response_blob'):
    assert token in active
for token in ('titan_linux_trace_events','titan_linux_trace_blob','titan_linux_trace_rsp_len','titan_linux_trace_rsp_off'):
    assert token not in active
assert 'TITAN_NETWORK_BOOT_GRACE_FRAMES 120u' in net
init=net.split('void TitanNetwork_Initialize(void)',1)[1].split('void TitanNetwork_Update(void)',1)[0]
assert 'TitanNetwork_InitInterfaces();' not in init
assert 'g_network_bootstrap_attempted=0' in init
interfaces=net.split('static void TitanNetwork_InitInterfaces(void)',1)[1].split('static int TitanNetwork_BindWifiRpc',1)[0]
assert 'TitanNetwork_EnsureWifiDriver()' in interfaces
assert 'TitanNetwork_EnsureEthernetDriver()' in interfaces
# one physical driver per preferred path, not eager-loading both unconditionally
assert 'if(g_network.preference==TITAN_NETWORK_PREFER_ETHERNET)' in interfaces
print('network-runtime-compact BF2 contract: PASS')
