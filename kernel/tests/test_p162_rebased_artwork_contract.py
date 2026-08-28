#!/usr/bin/env python3
from pathlib import Path
import hashlib, subprocess, sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
def need(rel,tok):
 s=(root/rel).read_text()
 if tok not in s: raise SystemExit(f'P162_ARTWORK_REBASE=FAIL {rel}: missing {tok}')
# P162.4 renderer/I8/native geometry must survive.
for tok in ('TitanVideo_GameUploadTextureI8','g_video_game_texture_large_psm8','game_vu1_active','game_path3_pending'):
 need('kernel/src/graphics/titan_video.c',tok)
# P162 transactional teardown must survive.
for tok in ('If VIF1/PATH1 ownership is still fault-latched','if (TitanGameRuntime_IsActive() && !TitanGameRuntime_End())','(void)TitanGameRuntime_End();'):
 need('kernel/src/services/titan_app_runtime.c',tok)
# Artwork closure additions.
for tok in ('TITAN_VIDEO_MEDIA_PRIMARY_WIDTH','primary_surface','legacy_surface'):
 need('kernel/src/graphics/titan_video.c' if tok!='TITAN_VIDEO_MEDIA_PRIMARY_WIDTH' else 'kernel/src/graphics/titan_video.h',tok)
for tok in ('background_width','TITAN_APP_BACKGROUND_PRIMARY_BYTES','runtime_class == TITAN_APP_RUNTIME_CLASS_GAME','TitanLocalization_Compat((text))'):
 need('kernel/src/services/titan_app_runtime.c',tok)
subprocess.run([sys.executable, str(Path(__file__).with_name('test_b30_release_app_artwork_contract.py')), str(root)], check=True)
print('P162_ARTWORK_REBASE=PASS preserves P162.4 renderer + transactional teardown + visual closure')
