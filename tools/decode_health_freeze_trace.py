#!/usr/bin/env python3
import struct
import sys
from pathlib import Path

MAGIC = 0x31544648
HEADER = struct.Struct('<8I')
EVENT = struct.Struct('<IIIHHIII')

NAMES = {
0x0100:'FRAME_BEGIN',0x0110:'EARLY_INPUT_ENTER',0x0111:'EARLY_INPUT_EXIT',
0x0120:'INPUT_ENTER',0x0121:'INPUT_EXIT',0x0130:'AUDIO_ENTER',0x0131:'AUDIO_EXIT',
0x0140:'SERVICES_ENTER',0x0141:'DOWNLOAD_ENTER',0x0142:'DOWNLOAD_EXIT',
0x0143:'STORE_ENTER',0x0144:'STORE_EXIT',0x0145:'DISC_ENTER',0x0146:'DISC_EXIT',
0x0147:'DIAG_ENTER',0x0148:'DIAG_EXIT',0x0149:'STORAGE_ENTER',0x014a:'STORAGE_EXIT',
0x014b:'ASSET_ENTER',0x014c:'ASSET_EXIT',0x014d:'NETWORK_ENTER',0x014e:'NETWORK_EXIT',
0x014f:'NET_UI_ENTER',0x0150:'NET_UI_EXIT',0x0151:'BROWSER_ENTER',0x0152:'BROWSER_EXIT',
0x0153:'SERVICES_EXIT',0x0160:'UI_ENTER',0x0161:'SHELL_UPDATE_ENTER',0x0162:'SHELL_UPDATE_EXIT',
0x0163:'UI_EXIT',0x0170:'RENDER_ENTER',0x0171:'VIDEO_BEGIN_ENTER',0x0172:'VIDEO_BEGIN_EXIT',
0x0173:'VIDEO_CLEAR',0x0174:'SHELL_RENDER_ENTER',0x0175:'SHELL_RENDER_EXIT',
0x0176:'OVERLAYS_ENTER',0x0177:'OVERLAYS_EXIT',0x0178:'VIDEO_END_ENTER',0x0179:'VIDEO_END_EXIT',
0x017a:'RENDER_EXIT',0x0180:'FRAME_END',0x1000:'STARTUP_HEALTH_TRANSITION',
0x1001:'STARTUP_AUDIO_BEFORE',0x1002:'STARTUP_AUDIO_AFTER',0x1003:'STARTUP_WIRELESS_BEFORE',
0x1004:'STARTUP_WIRELESS_AFTER',0x1010:'STARTUP_HEALTH_FRAME',0x1011:'STARTUP_WELCOME_TRANSITION',
0x1100:'WIRELESS_BEGIN',0x1101:'WIRELESS_BT_REQUEST_BEFORE',0x1102:'WIRELESS_BT_REQUEST_AFTER',
0x1103:'WIRELESS_BOOTSTRAP_BEFORE',0x1104:'WIRELESS_BOOTSTRAP_AFTER',0x1105:'WIRELESS_BOND_BEFORE',
0x1106:'WIRELESS_BOND_AFTER',0x1107:'WIRELESS_END',0x1200:'BT_REQUEST_ENTER',
0x1201:'BT_REQUEST_EARLY_WAIT',0x1202:'BT_WORKER_CREATE_BEFORE',0x1203:'BT_WORKER_CREATE_AFTER',
0x1210:'BT_WORKER_ENTER',0x1211:'BT_SMSUTILS_EXEC_BEFORE',0x1212:'BT_SMSUTILS_EXEC_AFTER',
0x1213:'BT_PADEMU_EXEC_BEFORE',0x1214:'BT_PADEMU_EXEC_AFTER',0x1215:'BT_WORKER_EXIT',
0x1300:'BT_BIND_BEFORE',0x1301:'BT_BIND_AFTER',0x1310:'WIRELESS_RPC_BEFORE',
0x1311:'WIRELESS_RPC_AFTER',0x1320:'BOND_RPC_BEFORE',0x1321:'BOND_RPC_AFTER',
0x1400:'NETWORK_BOOT_WORKER_CREATE_BEFORE',0x1401:'NETWORK_BOOT_WORKER_CREATE_AFTER',
0x1402:'NETWORK_BOOT_WORKER_ENTER',0x1410:'NETWORK_MODULE_SEARCH_BEFORE',
0x1411:'NETWORK_MODULE_SEARCH_AFTER',0x1420:'NETWORK_MODULE_EXEC_BEFORE',
0x1421:'NETWORK_MODULE_EXEC_AFTER',0x1428:'NETWORK_NETMAN_INIT_BEFORE',
0x1429:'NETWORK_NETMAN_INIT_AFTER',0x142a:'NETWORK_LINK_MODE_BEFORE',
0x142b:'NETWORK_LINK_MODE_AFTER',0x1430:'NETWORK_PS2IP_INIT_BEFORE',
0x1431:'NETWORK_PS2IP_INIT_AFTER',0x1440:'NETWORK_SELECT_IF_BEFORE',
0x1441:'NETWORK_SELECT_IF_AFTER',0x1450:'NETWORK_BOOT_WORKER_EXIT',
}

MODULE_NAMES = {1:'NETMAN',2:'DEV9',3:'SMAP',4:'AR9271',255:'OTHER'}


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f'usage: {Path(sys.argv[0]).name} health-freeze-trace.bin')
    data = Path(sys.argv[1]).read_bytes()
    if len(data) < HEADER.size:
        raise SystemExit('trace too small')
    fields = HEADER.unpack_from(data, 0)
    magic, version, count, newest, crit_seq, crit_code, req, flushed = fields
    if magic != MAGIC:
        raise SystemExit(f'bad magic 0x{magic:08x}')
    print(f'HFT version={version} event_count={count} newest={newest} '
          f'critical_seq={crit_seq} critical={NAMES.get(crit_code, hex(crit_code))} '
          f'requested={req} flushed={flushed}')
    print('seq      us_low     frame  code   checkpoint                         a          b          c')
    off = HEADER.size
    rows = []
    while off + EVENT.size <= len(data):
        seq, us, frame, code, _reserved, a, b, c = EVENT.unpack_from(data, off)
        if seq != 0:
            rows.append((seq, us, frame, code, a, b, c))
        off += EVENT.size
    # The trace is a circular ring. Physical slot order stops being chronological
    # after the first wrap, so always order by the committed sequence number.
    rows.sort(key=lambda row: row[0])
    for seq, us, frame, code, a, b, c in rows:
        name = NAMES.get(code, f'UNKNOWN_{code:04x}')
        if code in (0x1410,0x1411,0x1420,0x1421):
            name += '[' + MODULE_NAMES.get(a, str(a)) + ']'
        marker = ' <CRITICAL' if seq == crit_seq else ''
        print(f'{seq:8d} {us:10d} {frame:7d} 0x{code:04x} {name:34s} '
              f'{a:10d} {b:10d} {c:10d}{marker}')

if __name__ == '__main__':
    main()
