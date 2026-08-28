#!/usr/bin/env python3
from pathlib import Path
import re, sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
ds = (root / 'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()

def need(v, m):
    if not v:
        raise AssertionError(m)

def matching_brace(src, open_pos):
    depth=0; i=open_pos; mode='code'
    while i < len(src):
        c=src[i]; n=src[i+1] if i+1 < len(src) else ''
        if mode=='code':
            if c=='/' and n=='*': mode='block'; i+=2; continue
            if c=='/' and n=='/': mode='line'; i+=2; continue
            if c=='"': mode='string'; i+=1; continue
            if c=="'": mode='char'; i+=1; continue
            if c=='{': depth+=1
            elif c=='}':
                depth-=1
                if depth==0: return i
            i+=1
        elif mode=='block':
            if c=='*' and n=='/': mode='code'; i+=2
            else: i+=1
        elif mode=='line':
            if c=='\n': mode='code'
            i+=1
        else:
            q='"' if mode=='string' else "'"
            if c=='\\': i+=2
            elif c==q: mode='code'; i+=1
            else: i+=1
    raise AssertionError('unmatched brace')

def body_of(signature):
    m=re.search(re.escape(signature)+r'\s*\{', ds)
    need(m is not None, 'definition missing: '+signature)
    b=ds.find('{',m.start())
    e=matching_brace(ds,b)
    return ds[m.start():e+1]

need('P10.6.4: runtime Bulk-IN L2CAP RX rearm reliability.' in ds,
     'P10.6.4 marker missing')
need('#define P1064_L2CAP_REARM_ATTEMPTS 4u' in ds,
     'bounded rearm count missing')
need('#define P1064_L2CAP_REARM_DELAY_US 500u' in ds,
     'bounded retry delay missing')
need('static int p1064_l2cap_rx_arm_locked(void *arg, u32 origin)' in ds,
     'locked rearm helper missing')
need('static void p1064_l2cap_rx_recover_if_needed_locked(u32 origin)' in ds,
     'locked recovery helper missing')
need('static void p1064_l2cap_rx_recover_if_needed(u32 origin)' in ds,
     'safe polling recovery wrapper missing')
need('0x50313634u' in ds, 'P164 telemetry missing')
need('0x50313635u' in ds, 'P165 telemetry missing')

cb=body_of('static void l2cap_event_cb(int resultCode, int bytes, void *arg)')
need('(void)p1064_l2cap_rx_arm_locked(arg, 0u);' in cb,
     'L2CAP callback does not use checked rearm helper')
need('UsbBulkTransfer(bt_dev.inEndp' not in cb,
     'silent raw runtime Bulk-IN submit remains in callback')

hci=body_of('static void hci_event_cb(int resultCode, int bytes, void *arg)')
need('HCI_event_task(resultCode);' in hci,
     'HCI callback lost HCI_event_task')
need('p1064_l2cap_rx_recover_if_needed_locked(1u);' in hci,
     'HCI callback cannot recover pending L2CAP RX')

get_data=body_of('int ds34bt_get_data(u8 *dst, int size, int port)')
need('p1064_l2cap_rx_recover_if_needed_locked(2u);' in get_data,
     'game data poll cannot recover RX')

get_status=body_of('int ds34bt_get_status(int port)')
need('p1064_l2cap_rx_recover_if_needed_locked(3u);' in get_status,
     'status poll cannot recover RX')

tele=body_of('int ds34bt_get_telemetry(int port, void *out)')
need('p1064_l2cap_rx_recover_if_needed(4u);' in tele,
     'transport-only telemetry poll cannot recover RX')

cfg=body_of('static void bt_config_set(int result, int count, void *arg)')
need('g_p1064_l2cap_rearm_pending = 0u;' in cfg,
     'fresh USB configuration does not clear stale pending state')

disc=ds.index('case HCI_EVENT_DISCONN_COMPLETE:')
disc_tail=ds[disc:disc+1800]
need('0x50313635u' in disc_tail and '(u32)hci_buf[5]' in disc_tail,
     'disconnect reason is not recorded in actual HCI disconnect case')

need('iomanX_open(' not in ds and 'iomanX_write(' not in ds,
     'direct filesystem I/O returned to ds34bt')

print('b30-p1064-bt-rx-rearm-reliability: PASS definition-safe patch; checked/recoverable Bulk-IN RX; P165 disconnect reason')
