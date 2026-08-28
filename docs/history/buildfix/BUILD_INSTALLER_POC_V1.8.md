# Project Titan Installer POC v1.8

## Correções desta revisão

- Banco de identificação de Model ID ampliado com os IDs públicos conhecidos do MechaPwn.
- Fallback de família por faixa de Model ID e por ROMVER para IDs desconhecidos/emulados.
  - ROM 2.00 -> família Slim 70/75/77/79.
  - ROM 2.30+ -> família Slim 90.
- O caso visto no PCSX2 (`Model ID 0xAE06`, `ROM 0200EC20040614`) deixa de cair em FAT e passa a selecionar a ilustração Slim 70-family.
- Se nem Model ID nem ROMVER permitirem determinar a família, FAT / Slim70 / Slim90 continuam alternando com fade.
- O filtro/tint do background foi estendido além do canvas lógico para cobrir também as faixas de letterbox do modo 720p.
- Removido o `DelayThread(16000)` do loop principal. `TitanVideo_EndFrame()` já sincroniza com VSYNC; o delay extra estava reduzindo ~60 para ~30 FPS.
- Removido o rasterizador de ilustração pixel-a-pixel pela EE.
- As três artes agora são 128x128 A8 e são consolidadas uma vez em um atlas residente do GS usando `TitanVideo_UploadUiAtlas32`; cada frame desenha a arte com um único sprite `TitanVideo_DrawUiAtlasRegion32`.
- O script `generate_installer_assets.py` agora apenas recompila os PNGs aprovados para A8; ele não recria ilustrações procedurais.
- O marcador de instalação concluída agora usa `TITAN_INSTALLER_VERSION` e não uma versão antiga hard-coded.

## Validação local realizada

- `kernel/tests/test_installer_poc_v1.py` -> OK
- `kernel/tools/generate_installer_assets.py --output kernel/assets/ui/installer` -> OK
- `titan_installer_model.c` -> `gcc -fsyntax-only` com stubs PS2SDK -> OK
- `titan_installer.c` -> `gcc -fsyntax-only -D_EE -Werror=implicit-function-declaration` com stubs PS2SDK -> OK

## Compilação no PS2DEV

A fonte desta revisão possui o alvo `installer` no `kernel/Makefile`.

```bash
make -C bootloader clean
make -C bootloader
make -C bootloader deploy

make -C kernel clean
make -C kernel installer
```

Saída esperada na raiz:

```text
titan.elf
titan-installer.elf
```

O `kernel.elf` também permanece em `kernel/kernel.elf`.
