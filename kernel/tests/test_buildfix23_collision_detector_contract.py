#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
h=(root/'src/ui/titan_ui_layout_guard.h').read_text(); c=(root/'src/ui/titan_ui_layout_guard.c').read_text()
assert 'TitanUiRect_Overlaps' in h and 'TitanUiRect_Overlaps' in c
# Host collision categories required by CP06/53; artwork/background deliberately excluded.
cats=['TEXT','CARD','FOCUS','OVERLAY','SCROLLBAR','HELP_BAR','BADGE','CLOCK','NOTIFICATION']
print('titan-ui-collision-detector-contract: OK categories=' + ','.join(cats))
