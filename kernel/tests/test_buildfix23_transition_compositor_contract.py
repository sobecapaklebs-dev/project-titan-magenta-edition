#!/usr/bin/env python3
from pathlib import Path
import sys, subprocess, tempfile
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
h=(root/'src/ui/titan_ui_transition.h').read_text(); c=(root/'src/ui/titan_ui_transition.c').read_text(); scene=(root/'src/ui/titan_shell_scene.c').read_text(); core=(root/'src/core/titan_kernel.c').read_text()
for p in ('ENTER_BEGIN','ENTER_PROGRESS','ENTER_END','EXIT_BEGIN','EXIT_PROGRESS','EXIT_END'): assert 'TITAN_UI_TRANSITION_'+p in h
assert 'TitanUiTransition_Sample' in scene
assert 'source_visibility' in c and 'destination_visibility' in c
assert 'TitanVideo_Clear(0u, 0u, 0u);' in core
assert core.index('TitanVideo_Clear(0u, 0u, 0u);') < core.index('TitanShellScene_Render(')
assert 'TitanVideo_ResetTransform();' in scene
# Pure sampler must be deterministic and bounded.
program=r'''#include <assert.h>\n#include "titan_ui_transition.h"\nint main(void){TitanUiTransitionSample a,b; float p; for(p=0;p<=1.001f;p+=0.01f){TitanUiTransition_Sample(p,1,&a);TitanUiTransition_Sample(p,1,&b);assert(a.progress==b.progress);assert(a.source_visibility>=0&&a.source_visibility<=1);assert(a.destination_visibility>=0&&a.destination_visibility<=1);assert(a.wave_progress>=0&&a.wave_progress<=1);}return 0;}'''
# titan_animation SmoothStep is header-independent implementation; compile source with real animation module.
with tempfile.TemporaryDirectory() as d:
    src=Path(d)/'t.c'; src.write_text(program.replace('\\n','\n'))
    out=Path(d)/'t'
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I'+str(root/'src/ui'),str(src),str(root/'src/ui/titan_ui_transition.c'),str(root/'src/ui/titan_animation.c'),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('titan-ui-transition-compositor-contract: OK pure base+progress transforms, clear-before-compose, reset transforms')
