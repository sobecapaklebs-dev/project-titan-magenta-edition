from pathlib import Path
root=Path(__file__).resolve().parents[1]
scene=(root/'src/ui/titan_shell_scene.c').read_text()
video=(root/'src/graphics/titan_video.c').read_text()
hdr=(root/'src/graphics/titan_video.h').read_text()
assert '#define TITAN_DEBUG_SETTINGS_ROW_COUNT 12' in scene
assert 'labels[11] = "720p Composition"' in scene
assert 'Optimized 960x540' in scene and 'Reference' in scene
assert 'TitanVideo_Set720pValidationPath(next)' in scene
assert 'TitanVideo_Set720pValidationPath' in hdr and 'TitanVideo_Set720pValidationPath' in video
assert 'TitanVideoMode_GetOutput() != TITAN_VIDEO_OUTPUT_HDTV_720P' in video
assert 'g_video.hd_path_preference = previous' in video
assert 'TitanVideo_RestoreFramebufferConfig(&config)' in video
# Release default is optimized; REFERENCE remains selectable in DEX for A/B/fallback.
assert 'g_video.hd_path_preference = TITAN_VIDEO_720P_PATH_OPTIMIZED' in video
assert 'TITAN_VIDEO_720P_PATH_REFERENCE' in video
print('buildfix24 720p target selector contract: PASS')
