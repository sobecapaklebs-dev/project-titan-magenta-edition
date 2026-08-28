from pathlib import Path
import sys
k=Path(sys.argv[1]); c=(k/'src/graphics/titan_video.c').read_text(); h=(k/'src/graphics/titan_video_720p_transaction.c').read_text()
assert 'TitanVideo720pPath_Sanitize' in c
# Corrupt/unknown state must fail safe to the hardware-approved release path.
assert 'return TITAN_VIDEO_720P_PATH_OPTIMIZED' in h
assert 'g_video.hd_path_preference = TitanVideo720pPath_Sanitize' in c
print('titan-720p-invalid-preference-contract: PASS optimized fail-safe')
