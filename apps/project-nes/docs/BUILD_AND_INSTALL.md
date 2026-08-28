# Project Nes — Build, pacote e instalação

## Requisitos

- toolchain PS2DEV com `mips64r5900el-ps2-elf-gcc`;
- Python 3;
- Project Titan com suporte TPKG v2 / Titan App API v1.

O `.tmod` é um ELF32 MIPS `ET_REL`, não um ELF executável independente. Não use `ExecPS2` nem
linke o frontend contra funções privadas do kernel Titan.

## Build

```sh
make clean
make
make verify
make host-test
```

O Makefile:

1. compila `src/project_nes.c` para `project_nes.tmod`;
2. gera `icon.rgb565` 64x64;
3. regenera `ProjectNesTest.nes` a partir de código original;
4. monta `PROJECTNES.pkg` com `titan_pkg.py`;
5. permite verificar checksums/metadata com `make verify`;
6. `make host-test` executa os testes do core e da saída de áudio no PC.

## Instalação no PS2

Copie:

```text
build/PROJECTNES.pkg
```

para:

```text
mass:/TITAN/packages/PROJECTNES.pkg
```

No Project Titan:

```text
DEX Mode = Enabled
★ Debug Settings → Package Installer → PROJECTNES.pkg
```

Após instalar, a tile **Project Nes** aparece na HOME. O app instalado pode ser iniciado com DEX
ligado ou desligado; DEX é exigido pelo Titan apenas para instalar/desinstalar pacotes via fluxo de debug.

## ROM de usuário na preview 0.1

A preview detecta opcionalmente:

```text
mass:/TITAN/apps/PROJECTNES/data/roms/game.nes
```

O arquivo precisa ser iNES Mapper 0/NROM para o teste inicial. O pacote já inclui a ROM de diagnóstico,
portanto não é necessário adicionar nenhum arquivo para o primeiro boot.
