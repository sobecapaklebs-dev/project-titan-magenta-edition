#!/usr/bin/env python3
from pathlib import Path
import re, tempfile, subprocess, zlib, struct

root=Path(__file__).resolve().parents[2]
installer=(root/'kernel/src/installer/titan_installer.c').read_text(encoding='utf-8')
model=(root/'kernel/src/installer/titan_installer_model.c').read_text(encoding='utf-8')
main=(root/'kernel/main.c').read_text(encoding='utf-8')
make=(root/'kernel/Makefile').read_text(encoding='utf-8')

req=lambda c,m: (_ for _ in ()).throw(AssertionError(m)) if not c else None
canonical='''⚠ AVISO SOBRE A TECNOLOGIA VLOMBISCORE™

O Project Titan: Magenta Edition utiliza VlombisCore™ Technology, uma tecnologia avançada projetada para explorar capacidades anteriormente desconhecidas do PlayStation®2.

Durante a utilização, a Emotion Engine™ poderá apresentar níveis de emoção superiores aos especificados originalmente pelo fabricante.

Possíveis sintomas incluem:

* aumento espontâneo de performance;
* processamento excessivamente sentimental;
* apego emocional ao I/O Processor;
* execução de instruções motivada por saudade;
* crises existenciais durante operações de DMA;
* recusa ocasional em executar código por motivos pessoais.

Caso a Emotion Engine demonstre emoções intensas, não desligue o console imediatamente. Aguarde até que o processador consiga organizar seus sentimentos.

A equipe Project Titan não se responsabiliza por discussões entre a Emotion Engine e o Graphics Synthesizer.

VlombisCore™ Technology
Feel the processing.'''
# Exact semantic lines (UI uses bullets rather than markdown asterisks).
for line in [x for x in canonical.splitlines() if x and not x.startswith('* ')]:
    req(line in installer, f'canonical VlombisCore line missing: {line}')
for line in [x[2:] for x in canonical.splitlines() if x.startswith('* ')]:
    req(line in installer, f'canonical symptom missing: {line}')

req('{0xd42b, "SCPH-70000"}' in model, '70000 model ID mapping missing')
req('{0xd478, "SCPH-90006"}' in model, '90006 model ID mapping missing')
req('n >= 90000u' in model and 'n >= 70000u' in model, 'family split missing')
req('0xd475u' in model, '70k/90k fallback boundary missing')
req('sceCdReadNVM(version[1] < 4u ? 0xE4u : 0xF8u' in model, 'read-only Model ID query missing')
req('sceCdWriteNVM' not in model and 'sceCdWM' not in model, 'installer model detection must never write NVRAM')
req('mc%d:/BOOT/BOOT.ELF' in installer, 'bootloader destination is not Memory Card DEV1')
req('BOOT.TITAN.BAK' in installer, 'existing DEV1 boot backup missing')
req('TITAN_INSTALLER_CONTAINER' in installer and 'TITAN_SYSTEM.TINS' in installer,
    'single TINS installation container path missing')
req('TITAN_TINS_FLAG_AUTONOMOUS' in installer and '!s->autonomous_available' in installer,
    'autonomous mode is not gated on container metadata')
req('TITAN_INSTALLER_CONSUMED' in installer and 'remove(s->executable_path)' in installer,
    'one-shot self-invalidation missing')
req('TITAN_INSTALL_JOB_RECOVER' in installer and 'TITAN_INSTALL_JOB_ROLLBACK' in installer,
    'incremental transaction recovery/rollback missing')
req('TITAN_INSTALL_JOB_READ_SOURCE' in installer and 'TITAN_INSTALL_JOB_VERIFY_READ' in installer and '.TITAN.NEW' in installer and '.TITAN.BAK' in installer,
    'container-backed cooperative staged publication contract missing')
req('TitanInstaller_ShouldRun' in main and 'TitanKernel_Run' in main, 'normal kernel/installer dispatch missing')
req('installer: $(EE_BIN)' in make and '../titan-installer.elf' in make, 'installer make target missing')
mods=(root/'kernel/src/services/titan_iop_modules.c').read_text(encoding='utf-8')
prof=(root/'kernel/src/services/titan_profile_storage.c').read_text(encoding='utf-8')
req('src/services/titan_iop_modules.o' in make, 'single-owner embedded IOP module object missing')
req('src/installer/titan_install_container.o' in make, 'TINS parser object missing from kernel build')
for generated in ('titan_iomanx_irx.h','titan_filexio_irx.h','titan_usbd_irx.h','titan_usbhdfsd_irx.h'):
    req(generated in mods, f'central IOP module owner missing {generated}')
    req(generated not in installer, f'installer directly defines embedded module via {generated}')
    req(generated not in prof, f'profile storage directly defines embedded module via {generated}')
req('#include "titan_iop_modules.h"' in installer and '#include "titan_iop_modules.h"' in prof,
    'embedded IOP modules are not consumed through extern declarations')
# First interactive renderer must happen after post-IOP model detection.
pos_input=installer.index('result = TitanInput_Initialize(0,0);')
pos_detect=installer.index('TitanInstallerModel_Detect(&s.console);', pos_input)
pos_render=installer.index('render(&s);', pos_detect)
req(pos_input < pos_detect < pos_render, 'first interactive screen is rendered before reliable model detection')
req('TitanShellBackground_Draw' in installer and 'TitanTheme_Initialize();' in installer, 'installer must use the existing Magenta shell background/theme stack')
req('#include "titan_video_output.h"' in installer and 'TitanVideoOutput_Initialize();' in installer, 'installer video-output service is not initialized')
req('TITAN_INSTALLER_PAGE_VIDEO_OUTPUT = 0' in installer and 's.page = TITAN_INSTALLER_PAGE_VIDEO_OUTPUT;' in installer, 'video-output selection is not the first interactive page')
video_block=installer[installer.index('case TITAN_INSTALLER_PAGE_VIDEO_OUTPUT:'):installer.index('case TITAN_INSTALLER_PAGE_CONTROLLER:')]
for token in ('TitanVideoOutput_GetConfiguredOutputName()', 'TitanVideoOutput_GetRegionName()', 'TitanVideoOutput_CanTestProgressive()', 'Executar instalador em 720p Progressive'):
    req(token in video_block, f'video-output page missing: {token}')
req('TitanVideoOutput_SetShellProgressive(1)' in installer and 'TITAN_INSTALLER_VIDEO_TEST_FRAMES' in installer, 'temporary 720p test path missing')
req('TitanVideoOutput_SetShellProgressive(s.video_test_previous_progressive)' in installer, '720p timeout/cancel restore path missing')
req('draw_controller_instruction_block' in installer and 'Connect the DUALSHOCK 2 analog controller' in installer and 'Conecte o controle analógico DUALSHOCK 2' in installer, 'Orbis-style multilingual controller screen missing')
controller_block=installer[installer.index('case TITAN_INSTALLER_PAGE_CONTROLLER:'):installer.index('case TITAN_INSTALLER_PAGE_LANGUAGE:')]
for forbidden in ('draw_header(', 'draw_panel(', 'draw_center_button(', 'Console detectado:', 'draw_footer('):
    req(forbidden not in controller_block, f'controller screen contains non-Orbis chrome: {forbidden}')
req('TitanVideo_SetTransform' in installer and 'page_transition_frame' in installer, 'page slide/fade transition missing')
req('TitanVideo_UploadUiAtlas32' in installer and 'TitanVideo_DrawUiAtlasRegion32' in installer,
    'installer hero artwork is not using the resident GS UI-atlas path')
req('DrawPixelPhysicalAlpha' not in installer, 'installer must not rasterize hero artwork pixel-by-pixel on the EE')
req('DelayThread(16000)' not in installer, 'installer frame loop must rely on TitanVideo_EndFrame vsync, not add a second 16ms delay')
req('TitanVideo_DrawRectAlpha(0.0f, -32.0f, 854.0f, 512.0f' in installer,
    'fullscreen tint does not cover the 720p letterbox area')
req('family_from_romver' in model and 'version >= 200u' in model and 'version >= 230u' in model,
    'ROMVER family fallback missing for unknown/emulated model IDs')
req('{0xd404, "SCPH-50000 MB/NH"}' in model and '{0xd42f, "SCPH-70000 GT"}' in model and '{0xd48d, "SCPH-90008 SS"}' in model,
    'expanded MechaPwn model-ID database is incomplete')
for generated in ('titan_installer_fat_data.h','titan_installer_slim70_data.h','titan_installer_slim90_data.h'):
    req(generated in installer, f'installer illustration asset header missing: {generated}')
for raw in ('assets/ui/installer/titan_installer_fat.a8','assets/ui/installer/titan_installer_slim70.a8','assets/ui/installer/titan_installer_slim90.a8'):
    req(raw in make, f'installer illustration asset rule missing: {raw}')
req('$(TITAN_INSTALLER_FAT_HEADER): $(TITAN_INSTALLER_FAT_RAW)' in make, 'fat illustration bin2c rule missing')
req('$(TITAN_INSTALLER_SLIM70_HEADER): $(TITAN_INSTALLER_SLIM70_RAW)' in make, 'slim70 illustration bin2c rule missing')
req('$(TITAN_INSTALLER_SLIM90_HEADER): $(TITAN_INSTALLER_SLIM90_RAW)' in make, 'slim90 illustration bin2c rule missing')
dep_block = make[make.index('src/installer/titan_installer.o:'):make.index('font-atlas:')]
for token in ('$(TITAN_INSTALLER_FAT_HEADER)','$(TITAN_INSTALLER_SLIM70_HEADER)','$(TITAN_INSTALLER_SLIM90_HEADER)'):
    req(token in dep_block, f'installer object dependency missing: {token}')

# Exercise host media builder against a synthetic runtime and verify the single TINS container.
with tempfile.TemporaryDirectory(prefix='titan-installer-test-') as td:
    td=Path(td); runtime=td/'runtime'; (runtime/'TITAN').mkdir(parents=True)
    (runtime/'kernel.elf').write_bytes(b'kernel-test')
    (runtime/'TITAN'/'config.dat').write_bytes(b'config-test')
    installer_elf=td/'titan-installer.elf'; installer_elf.write_bytes(b'installer-test')
    boot=td/'BOOT.ELF'; boot.write_bytes(b'boot-test')
    out=td/'out'
    cmd=['python3',str(root/'tools/build_titan_installer_media.py'),'--project',str(root),'--runtime',str(runtime),
         '--installer-elf',str(installer_elf),'--bootloader-elf',str(boot),'--output',str(out)]
    subprocess.run(cmd,check=True,capture_output=True,text=True)
    install_root=out/'TITAN_INSTALL'
    req((install_root/'titan-installer.elf').is_file(), 'installer ELF missing from media')
    req((install_root/'TITAN_SYSTEM.TINS').is_file(), 'single TINS container missing')
    req(not (install_root/'payload').exists(), 'legacy payload directory must not be generated')
    tins=(install_root/'TITAN_SYSTEM.TINS').read_bytes()
    req(tins[:4]==b'TINS', 'TINS magic mismatch')
    fmt,header_size,entry_size,count,flags,table_off,payload_off,total_size,table_crc=struct.unpack_from('<IIIIIIIII',tins,4)
    req(fmt==1 and header_size==64 and entry_size==224, 'TINS header contract mismatch')
    req(total_size==len(tins) and table_off==64 and payload_off==64+count*224, 'TINS layout mismatch')
    table=tins[table_off:payload_off]
    req((zlib.crc32(table)&0xffffffff)==table_crc, 'TINS table CRC mismatch')
    names=[]
    for idx in range(count):
        off=table_off+idx*224
        kind,eflags,rel,size,crc,path_len=struct.unpack_from('<IIIIII',tins,off)
        name=tins[off+24:off+24+path_len].decode('utf-8')
        payload=tins[payload_off+rel:payload_off+rel+size]
        req((zlib.crc32(payload)&0xffffffff)==crc, f'TINS entry CRC mismatch: {name}')
        names.append((kind,name))
    req((1,'kernel.elf') in names, 'kernel missing from TINS')
    req((2,'BOOT.ELF') in names, 'uppercase BOOT.ELF missing from TINS')

    # Standalone verifier must accept the generated container.
    subprocess.run(['python3',str(root/'tools/titan_tins.py'),'verify',str(install_root/'TITAN_SYSTEM.TINS')],check=True,capture_output=True,text=True)

print('installer-poc-v1-contract: OK')
