from pathlib import Path
import sys
k=Path(sys.argv[1]); b=(k/'src/ui/titan_shell_background.c').read_text(); sg=(k/'src/services/titan_theme_scene_graph.h').read_text()
assert 'TITAN_THEME_QUALITY_REDUCED' in sg and 'TitanThemeDynamic_ApplyPressure' in sg
assert 'minimum visual identity' in b
assert 'TITAN_THEME_MAX_RESIDENT_FRAMES 3u' in sg
print('titan-theme-boot-degradation-contract: PASS')
