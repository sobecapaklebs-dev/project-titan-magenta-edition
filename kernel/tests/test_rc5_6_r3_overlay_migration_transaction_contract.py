#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
dex=(root/'kernel/src/services/titan_dex.c').read_text(encoding='utf-8')
version=(root/'kernel/include/titan_version.h').read_text(encoding='utf-8')

def req(ok,msg):
    if not ok:
        raise SystemExit(msg)

start=dex.index('/* RC5.6 one-time migration:')
end=dex.index('    TitanDex_UpdateRamUsage();', start)
block=dex[start:end]

req('overlay_saved = TitanDex_SaveOverlaySetting();' in block,
    'overlay migration does not capture persistence result')
req('if (overlay_saved)' in block,
    'schema commit is not gated by verified overlay persistence')
marker=block.index('TitanDex_SaveBooleanSetting(\n                        TITAN_DEX_OVERLAY_SCHEMA_PATH')
gate=block.index('if (overlay_saved)')
req(gate < marker,
    'schema marker can be committed before overlay OFF persistence succeeds')
req(block.count('TitanStorageIo_Remove(TITAN_DEX_OVERLAY_SCHEMA_PATH);') >= 2,
    'failed/partial migration does not remove schema marker for retry')
req('g_dex.overlay_enabled = 0;' in block,
    'migration does not remain fail-safe OFF in RAM')
req(('RC5.6-netman-scan-perf-r3' in version) or
    ('RC5.6-netman-rpc-authority-r4' in version) or
    ('RC5.6-smap-mainif-r5' in version) or
    ('RC5.6-iop-search-zero-r6' in version) or
    ('RC5.6-exec-authority-smap-rebind-r7' in version) or
    ('RC5.6-dev9-exec-state-r8' in version) or
    ('RC5.6-network-audit-final' in version) or
    ('RC5.6-network-hwtrace-final' in version),
    'recovery build identity is not compatible with r3/r4/r5/r6/r7/r8/final')

print('rc5.6-r3-overlay-migration-transaction: PASS')
