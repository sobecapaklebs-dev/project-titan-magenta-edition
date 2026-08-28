# Project Titan Installer POC v1.9 — Sharp hero assets

## Correção principal
A arte do PS2 na tela de conexão estava sendo reduzida para 128x128 e depois ampliada novamente em 720p, causando blur excessivo.

A v1.9 altera somente o caminho visual dessas ilustrações:
- assets FAT / Slim 70000 / Slim 90000 recompilados em 256x256 a partir das artes aprovadas;
- atlas do Installer passa a carregar duas artes 256x256 por vez em um atlas 512x256;
- modelo conhecido usa um único slot 256x256;
- modelo desconhecido usa dois slots 256x256 para crossfade entre o modelo atual e o próximo;
- novo draw path `TitanVideo_DrawUiAtlasRegion32Point()` usa nearest/point sampling para line art, sem alterar o filtro padrão dos demais assets da shell;
- dimensão lógica da arte foi reduzida levemente para 280x280 para reduzir upscale em 720p;
- upload do atlas continua acontecendo fora do frame e somente quando o par de imagens muda.

## Não alterado
- detecção de modelo da v1.8;
- fullscreen tint;
- fluxo do instalador;
- transições;
- renderer de waves;
- política de instalação/rollback;
- correção de performance que levou o Installer a 60 FPS no teste do PCSX2.

## Build
```bash
make -C bootloader clean
make -C bootloader
make -C bootloader deploy

make -C kernel clean
make -C kernel installer
```
