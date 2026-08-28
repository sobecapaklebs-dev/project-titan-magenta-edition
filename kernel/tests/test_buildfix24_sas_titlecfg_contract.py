#!/usr/bin/env python3
from pathlib import Path
import sys
k=Path(sys.argv[1]).resolve(); c=(k/'src/services/titan_applications.c').read_text()
pos_title=c.index('directory, "title.cfg"'); pos_app=c.index('directory, "app.cfg"', pos_title)
assert pos_title < pos_app and 'strncmp(line, "boot=", 5u)' in c and 'sas_title_has_boot' in c
print('titan-sas-titlecfg-contract: OK canonical title.cfg precedes Titan app.cfg fallback and tracks boot=')
