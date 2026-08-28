#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
video = (root / 'src/graphics/titan_video.c').read_text()

def req(cond, msg):
    if not cond:
        raise SystemExit(msg)

end = video[video.index('TitanResult TitanVideo_EndFrame'):video.rindex('#endif')]
scale = video[video.index('static TitanResult TitanVideo_ScaleOptimized720pToOutput'):video.index('TitanResult TitanVideo_EndFrame')]

req('result = TitanVideo_SubmitPacket(1);' in end,
    'optimized 720p composition pass is not FINISH-fenced before render-to-texture')
req('result = TitanVideo_ScaleOptimized720pToOutput();' in end,
    'optimized 720p presentation pass missing')
req('TitanVideo_GameSetAd(cursor, 1u, GS_REG_TEXFLUSH)' in scale,
    'optimized 720p scale pass lacks normal-packet TEXFLUSH after render-target completion')
req('result = TitanVideo_SubmitPacket(1);' in scale,
    'optimized 720p presentation pass lacks final GS FINISH fence')
req('DMA/GIF ordering and' in end and 'not a render-target completion barrier' in end,
    'source no longer documents the hardware render-to-texture hazard')
print('rc1-720p-render-to-texture-barrier-contract: PASS FINISH + TEXFLUSH + final FINISH')
