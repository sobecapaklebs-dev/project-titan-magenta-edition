#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
h=(root/'src/graphics/titan_video_720p_plan.h').read_text()
c=(root/'src/graphics/titan_video_720p_plan.c').read_text()
for token in (
    'unsigned int internal_width;', 'unsigned int internal_height;',
    'unsigned int output_width;', 'unsigned int output_height;',
    'TITAN_VIDEO_720P_PATH_REFERENCE', 'TITAN_VIDEO_720P_PATH_OPTIMIZED'):
    assert token in h, token
for token in (
    'plan->internal_width = 960u;', 'plan->internal_height = 540u;',
    'plan->output_width = 1280u;', 'plan->output_height = 720u;',
    'plan->internal_buffer_count = 1u;', 'plan->output_buffer_count = 1u;'):
    assert token in c, token
assert 'plan->internal_width = plan->output_width' not in c
assert 'plan->internal_height = plan->output_height' not in c
print('pikavr-internal-output-resolution-contract: OK internal and physical output are explicit independent dimensions')
