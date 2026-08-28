#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]
diag=(r/'kernel/src/services/titan_diagnostics.c').read_text(); net=(r/'kernel/src/services/titan_network.c').read_text(); pik=(r/'kernel/src/graphics/titan_pikavr.c').read_text(); vlo=(r/'kernel/src/services/titan_vlombiscore.c').read_text(); ov=(r/'kernel/src/services/titan_ingame_overlay.c').read_text(); oh=(r/'kernel/src/services/titan_platform_overhead.c').read_text()
assert 'TITAN_DIAG_PERCENTILE_SAMPLE_FRAMES 8u' in diag and 'frame_index % TITAN_DIAG_PERCENTILE_SAMPLE_FRAMES' in diag
assert 'TITAN_DIAG_SERVICE_SAMPLE_FRAMES 30u' in diag
assert 'if(g_network.state!=TITAN_NETWORK_OFFLINE)' in net
assert 'if (!g_pikavr.optimized_path || fingerprint == 0 || texture_handle == 0)' in pik
assert 'if (capability_mask == 0u' in vlo
assert 'if (!g_overlay.open)' in ov and 'if (!g_overlay.open) return;' in ov
assert 'if(!g_overhead.enabled' in oh and 'return 0u' in oh
print('buildfix28-fast-paths-contract: OK')
