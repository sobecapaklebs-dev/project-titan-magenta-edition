from pathlib import Path
import sys
k=Path(sys.argv[1]); c=(k/'src/graphics/titan_video.c').read_text()
fn=c[c.index('TitanResult TitanVideo_Initialize'):c.index('TitanResult TitanVideo_BeginGameMode')]
# Superseded by BF30 real-hardware acceptance: optimized 960x540 composition is
# now the release/default 720p policy; REFERENCE is retained for DEX A/B.
assert 'g_video.hd_path_preference = TITAN_VIDEO_720P_PATH_OPTIMIZED' in fn
assert 'Physical output remains 1280x720p' in fn
print('titan-720p-release-optimized-default-contract: PASS')
