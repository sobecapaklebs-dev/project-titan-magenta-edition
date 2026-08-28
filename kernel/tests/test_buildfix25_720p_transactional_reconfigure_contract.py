from pathlib import Path
import sys
k=Path(sys.argv[1]); c=(k/'src/graphics/titan_video.c').read_text()
fn=c[c.index('TitanResult TitanVideo_Set720pValidationPath'):c.index('TitanVideo720pPath TitanVideo_Get720pPath')]
assert 'rollback_result' in fn
assert 'g_video.hd_path_preference = previous' in fn
assert 'if (rollback_result == TITAN_RESULT_OK)' in fn
assert 'g_video.initialized = 1' in fn
assert 'g_video.initialized = 0' in fn
print('titan-720p-transactional-reconfigure-contract: PASS')
