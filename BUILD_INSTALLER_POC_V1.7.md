# Project Titan Installer POC v1.7 — build

## O que mudou nesta revisão
- Assets da tela de conexão do controle substituídos pelas ilustrações enviadas para:
  - PS2 FAT
  - PS2 Slim 70000
  - PS2 Slim 90000
- Fundo removido dos 3 assets; linhas convertidas para branco.
- Quando o modelo do PS2 não é detectado, a tela do controle alterna entre as 3 artes com fade in/fade out.
- Botões/listas selecionáveis do instalador agora usam moldura no estilo Orbis/Project Titan, sem fundo branco sólido.
- Transição entre páginas reduzida e suavizada com slide horizontal + fade mais leve.

## Build manual
> Compile primeiro o bootloader e depois o kernel.

```bash
cd bootloader
make clean
make

cd ../kernel
make clean
make
```

## Artefatos principais
- `bootloader/stage2/stage2.elf`
- `kernel/kernel.elf`

## Observações
- Os assets `.a8` já estão incluídos em `kernel/assets/ui/installer/`.
- O `Makefile` do kernel continua gerando os headers `src/graphics/generated/titan_installer_*_data.h` a partir dos `.a8`.
