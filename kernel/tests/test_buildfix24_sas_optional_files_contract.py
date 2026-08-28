#!/usr/bin/env python3
from pathlib import Path
import sys
k=Path(sys.argv[1]).resolve(); h=(k/'src/services/titan_applications.h').read_text(); c=(k/'src/services/titan_applications.c').read_text()
for n in ['credits[TITAN_APPLICATION_CREDITS_CAPACITY]','source[TITAN_APPLICATION_SOURCE_CAPACITY]','strncmp(line, "credits=", 8u)','strncmp(line, "source=", 7u)']:
 assert n in h+c
print('titan-sas-optional-files-contract: OK AUTHOR/credits/source metadata tolerated without defining a new standard')
