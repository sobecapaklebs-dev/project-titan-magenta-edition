#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
core=(root/'apps/project-nes/src/project_nes_core.h').read_text()
app=(root/'apps/project-nes/src/project_nes.c').read_text()
ad=(root/'apps/project-nes/src/project_nes_fceumm_provider.c').read_text()
mk=(root/'apps/project-nes/Makefile').read_text()
req=lambda ok,msg: (_ for _ in ()).throw(AssertionError(msg)) if not ok else None
req('void (*shutdown)(void *context);' in core,'backend shutdown lifecycle tail missing')
req('#define PN_AUDIO_FRAMES 1024u' in app,'frontend audio capacity not raised for 48k core batches')
req('provider_frame_ready' in app and 'if (g_pn.provider_frame_ready)' in app,'frame presentation still legacy-owned')
shutdown_pos=app.find('g_pn.backend->shutdown(pn_backend_ctx())')
release_pos=app.find('api->release(g_pn.rom_data)',shutdown_pos)
req(shutdown_pos>=0 and release_pos>shutdown_pos,'provider shutdown must precede ROM release')
for forbidden in ('TitanAppApiV1','fileXio','SifLoadModule','gsKit','socket('):
    req(forbidden not in ad,f'FCEUmm adapter crosses Titan ownership boundary: {forbidden}')
for needed in ('retro_set_environment','retro_load_game','retro_run','retro_get_memory_data','retro_unload_game','retro_deinit'):
    req(needed in ad,f'missing libretro bridge primitive {needed}')
req('PROJECT_NES_FCEUMM_LINKED' in ad,'adapter must remain link-gated')
req('host_fceumm_adapter_test' in mk and '-fsanitize=address,undefined' in mk,'adapter sanitizer fixture not permanent')
print('buildfix27-project-nes-fceumm-adapter: OK libretro isolation/lifecycle/frame/audio contract')
