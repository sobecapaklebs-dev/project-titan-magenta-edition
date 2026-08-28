#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
video=(root/'src/graphics/titan_video.c').read_text()
start=video.index('static TitanResult TitanVideo_ScaleOptimized720pToOutput(void)')
end=video.index('\nTitanResult TitanVideo_EndFrame(void)', start)
scale=video[start:end]
assert 'draw_texture_flush(cursor)' not in scale, 'chain-only draw_texture_flush leaked into normal draw packet'
assert 'GS_REG_TEXFLUSH' in scale, 'normal GIF TEXFLUSH missing'
assert 'TitanVideo_GameSetAd(cursor, 1u, GS_REG_TEXFLUSH)' in scale, 'TEXFLUSH must be an ordinary AD payload'
assert 'result = TitanVideo_SubmitPacket(1);' in scale, 'final FINISH fence missing'
print('rc1-720p-normal-packet-texflush-contract: PASS normal GIF TEXFLUSH without DMATAG_END')
