#!/usr/bin/env python3
from pathlib import Path
import re,sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
early=(root/'kernel/src/services/titan_early_input.c').read_text()
wire=(root/'modules/titan-ul-engine/include/titan_early_input_wire.h').read_text()
p=(root/'modules/titan-ul-engine/modules/pademu/pademu.c').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()

s=early.find('static unsigned int make_pademu_args')
e=early.find('static int do_load',s)
assert s>=0 and e>s
a=early[s:e]
assert "out[0]='t'" not in a and "out[1]='\\0'" not in a
assert 'unsigned int n=8u+sizeof(ext);' in a
assert 'memcpy(out,&settings,4);' in a
assert 'memcpy(out+4,&macro,4);' in a
assert 'memcpy(out+8,&ext,sizeof(ext));' in a
assert 'settings|=TITAN_PADEMU_SETTINGS_TITAN_TRANSPORT_ONLY;' in a
assert 'TITAN_PADEMU_SETTINGS_SUPPRESS_SIO2_HOOK' not in wire

m=re.search(r'int\s+_start\s*\([^)]*\)\s*\{',p)
assert m
hook=p.find('int install_sio2hook()',m.start())
start=p[m.start():hook]
assert re.search(r'if\s*\(\s*!vmc\s*&&\s*!titan_transport_only\s*\)',start)
assert 'PAD_INIT(pad_enable, pad_options)' not in start

# P10.2 keeps P9.1's late-init invariant but makes the late init itself
# asynchronous so an already-present dongle cannot block the SIF RPC server.
assert 'static void pademu_transport_init_worker(void *arg)' in p
assert 'ok = PAD_INIT(pad_enable, pad_options);' in p
assert 'int pademu_transport_request_init(void)' in p
assert 'int pademu_transport_init_ready(void)' in p
assert 'int pademu_transport_init_on_demand(void)' in p
assert 'return pademu_transport_request_init();' in p
assert 'CreateThread(&t)' in p and 'StartThread(tid, 0)' in p
assert 'TITAN_PADEMU_TRACE_PAD_INIT_BEGIN' in p
assert 'TITAN_PADEMU_TRACE_PAD_INIT_END' in p

assert 'extern int pademu_transport_init_on_demand(void);' in rpc
assert 'extern int pademu_transport_init_ready(void);' in rpc
pair=rpc[rpc.find('if (fno == TITAN_PADEMU_RPC_BT_BEGIN_PAIRING)'):]
assert 'requested = pademu_transport_init_on_demand();' in pair
assert 'ready = pademu_transport_init_ready();' in pair
assert re.search(r'if\s*\(ready\)[\s\S]{0,1024}?ds34bt_pairing_available\s*\(\s*\)', pair)
assert 'PAD_INIT(' not in rpc

raw=(3 | (3<<8) | 0x00800000).to_bytes(4,'little')+(0xAB).to_bytes(4,'little')
assert raw==bytes([3,3,0x80,0,0xAB,0,0,0])
print('b30-p91-pair-on-demand: PASS raw OPL wire; zero PAD_INIT in _start/RPC; late init owned by dedicated IOP worker')
