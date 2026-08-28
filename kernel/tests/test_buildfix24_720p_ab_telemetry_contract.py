from pathlib import Path
root=Path(__file__).resolve().parents[1]
scene=(root/'src/ui/titan_shell_scene.c').read_text()
report=(root.parent/'PIKAVR_720P_TARGET_READINESS.md').read_text()
for token in ['720p path: %s','Internal resolution: %ux%u','Output resolution: %ux%u','VRAM used/free: %u / %u','Reserved headroom: %u','Texture residency: %u','Frame time: %u us','Scale mode: %s']:
    assert token in scene, token
for screen in ['Welcome','Profile Selection','Character/Avatar Selection','HOME','TOP Menu','Settings','Library','Applications','Package Installer','Notifications','Theme selection','Saved Data/VMC','Overview/Information','appdatahome','ftphome','DEX overlay','dialogs','keyboard','download/install UI']:
    assert screen in report, screen
assert 'Release/default path: OPTIMIZED 960x540 composition -> 1280x720 output' in report
assert 'No unchecked target item is a PASS' in report
print('buildfix24 720p A/B telemetry + screen matrix contract: PASS')
