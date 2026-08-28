#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
project=root.parent
video=(root/'src/graphics/titan_video.c').read_text()
trans=(root/'src/graphics/titan_video_720p_transaction.c').read_text()
theme=(root/'src/services/titan_theme.c').read_text()

def req(cond,msg):
    if not cond: raise SystemExit(msg)

init=video[video.index('TitanResult TitanVideo_Initialize'):video.index('TitanResult TitanVideo_BeginGameMode')]
end=video[video.index('TitanResult TitanVideo_EndFrame'):video.rindex('#endif')]
scale=video[video.index('static TitanResult TitanVideo_ScaleOptimized720pToOutput'):video.index('TitanResult TitanVideo_EndFrame')]
req('g_video.hd_path_preference = TITAN_VIDEO_720P_PATH_OPTIMIZED' in init,'optimized 720p is not cold-boot default')
req('result = TitanVideo_SubmitPacket(1);' in end,'render-to-texture path lacks mandatory GS FINISH barrier')
req('TitanVideo_GameSetAd(cursor, 1u, GS_REG_TEXFLUSH)' in scale,'optimized composition lacks normal-packet TEXFLUSH before local->texture sampling')
req('result = TitanVideo_SubmitPacket(1);' in scale,'optimized path lacks final GS fence')
req('return TITAN_VIDEO_720P_PATH_OPTIMIZED;' in trans,'invalid 720p preference does not fail safe to optimized')
for tok in ('TITAN_THEME_BACKGROUND_FLOW3D','wave->snake_enabled=1u','wave->reference_ribbon_enabled=0u',
            'theme->flow.vu1_deform_speed=0.72f','theme->flow.vu1_deform_frequency=1.5f',
            'theme->flow.vu1_deform_amplitude=7.2f'):
    req(tok in theme,f'built-in hardware-proven Flow profile missing: {tok}')
for folder in ('ribbon-scarlet','ribbon-wine','ribbon-cyan','ribbon-twilight'):
    cfg=(project/'themes'/folder/'theme.cfg').read_text()
    for tok in ('background_engine=flow3d-v1','primary_wave1_snake=1','primary_wave1_reference_ribbon=0',
                'vu1_deform_speed_milli=720','vu1_deform_frequency_milli=1500','vu1_deform_amplitude_milli=7200'):
        req(tok in cfg,f'{folder}: {tok} missing')
print('bf30-hwfix2-performance-regression-contract: PASS optimized 720p + GS render-to-texture barrier + restored Snake/VU1 Flow')
