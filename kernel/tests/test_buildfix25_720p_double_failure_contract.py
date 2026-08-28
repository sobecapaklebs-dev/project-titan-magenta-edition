from pathlib import Path
import sys
k=Path(sys.argv[1]); c=(k/'src/graphics/titan_video.c').read_text(); fn=c[c.index('TitanResult TitanVideo_Set720pValidationPath'):c.index('TitanVideo720pPath TitanVideo_Get720pPath')]
assert 'Primary and rollback both failed' in fn
assert 'g_video.initialized = 0' in fn and 'g_video.packet_error = 1' in fn
assert 'return rollback_result' in fn
print('titan-720p-double-failure-contract: PASS')
