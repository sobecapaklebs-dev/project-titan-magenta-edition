from pathlib import Path
root=Path(__file__).resolve().parents[1]
scene=(root/'src/ui/titan_shell_scene.c').read_text()
for token in ['720p path: %s','Internal resolution: %ux%u','Output resolution: %ux%u','VRAM used/free: %u / %u','Reserved headroom: %u','Texture residency: %u','Frame time: %u us','Scale mode: %s']:
    assert token in scene, token
print('buildfix24 720p A/B telemetry + screen matrix contract: PASS')
