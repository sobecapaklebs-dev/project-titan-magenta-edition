#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]
pik=(r/'kernel/src/graphics/titan_pikavr.h').read_text()
tex=(r/'kernel/src/graphics/titan_video_game_texture.c').read_text()
vlh=(r/'kernel/src/services/titan_vlombiscore.h').read_text()
vlc=(r/'kernel/src/services/titan_vlombiscore.c').read_text()
# pikaVR remains a single manager layered on generation-checked texture handles.
for token in ('TitanPikaVr_TryReuseTexture','TitanPikaVr_RegisterTexture','TitanPikaVr_ReleaseTexture','TitanPikaVr_RecordBackingReclaimed','TitanPikaVr_SetVramAccounting','TitanPikaVr_BuildExactPaletteRgba32','TitanPikaVr_RecordSamplerState'):
    assert token in pik
assert 'generation = handle >> TITAN_VIDEO_GAME_TEXTURE_INDEX_BITS' in tex
assert 'texture->generation != generation' in tex
# Pre-freeze Vlombis taxonomy is explicit and append-only; legacy names remain aliases.
for token in ('TITAN_VLOMBIS_CAP_STORAGE_QOS','TITAN_VLOMBIS_CAP_RENDER_STATE_CACHE','TITAN_VLOMBIS_CAP_ASYNC_PACKAGE_IO','TITAN_VLOMBIS_CAP_LIBRARY_INCREMENTAL_SCAN','TITAN_VLOMBIS_CAP_AUDIO_BUFFER_QOS','TITAN_VLOMBIS_CAP_NETWORK_BACKGROUND_QOS','TITAN_VLOMBIS_CAP_FRAME_BUDGETING','TITAN_VLOMBIS_CAP_PIKAVR'):
    assert token in vlh
assert '#define TITAN_VLOMBIS_CAP_STORAGE_BUDGETING    TITAN_VLOMBIS_CAP_STORAGE_QOS' in vlh
assert 'if (capability_mask == 0u' in vlc
print('buildfix28-prefreeze-contract: OK')
