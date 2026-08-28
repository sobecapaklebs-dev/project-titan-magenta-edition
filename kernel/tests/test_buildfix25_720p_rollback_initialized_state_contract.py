from pathlib import Path
import sys
k=Path(sys.argv[1]); c=(k/'src/graphics/titan_video.c').read_text()
restore=c[c.index('static TitanResult TitanVideo_RestoreFramebufferConfig'):c.index('static TitanResult TitanVideo_SwitchFramebufferConfig')]
assert 'g_video.initialized = 1' in restore
assert 'g_video.bound_texture = TITAN_VIDEO_TEXTURE_NONE' in restore
assert 'g_video.packet_error = 0' in restore
print('titan-720p-rollback-initialized-state-contract: PASS')
