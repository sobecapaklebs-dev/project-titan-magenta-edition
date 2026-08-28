from pathlib import Path
import sys
k=Path(sys.argv[1]); c=(k/'src/graphics/titan_video.c').read_text()
fn=c[c.index('TitanResult TitanVideo_Set720pValidationPath'):c.index('TitanVideo720pPath TitanVideo_Get720pPath')]
needle='if (TitanVideoMode_GetOutput() != TITAN_VIDEO_OUTPUT_HDTV_720P)'
pos=fn.index(needle); tail=fn[pos:fn.index('config = TitanVideo_GetFramebufferConfig',pos)]
assert 'g_video.hd_path_preference = path' in tail
assert 'TitanVideo_RestoreFramebufferConfig' not in tail
assert 'TitanVideoMode_BeginReconfigure' not in tail
print('titan-480i-validation-selector-isolation-contract: PASS')
