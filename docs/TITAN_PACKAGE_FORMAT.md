# Titan application package format — TPKG v1/v2

TPKG é o formato instalável do Project Titan. Ele permanece propositalmente
**sem compressão**, permitindo validar CRC e extrair em pequenos blocos no PS2.

- **TPKG v1**: compatibilidade com pacotes ELF antigos.
- **TPKG v2**: metadata ampliada, ícone/background e suporte a Titan-native
  `.tmod`.

A instalação só pode ser iniciada pelo Package Installer com **DEX Mode ativo**.

## Layout USB

Entrada:

```text
mass:/TITAN/packages/*.pkg
```

Transação de instalação:

```text
mass:/TITAN/apps/.install-TITLEID/    staging, nunca catalogado
mass:/TITAN/apps/TITLEID/             aplicação publicada
mass:/TITAN/apps/TITLEID/titan_app.ini
```

Um `TITLEID` já publicado não é sobrescrito silenciosamente.

## Header comum

Todos os inteiros são `u32` little-endian. Header fixo: **256 bytes**.

| Offset | Size | Campo |
|---:|---:|---|
| 0 | 4 | magic `TPKG` |
| 4 | 4 | versão (`1` ou `2`) |
| 8 | 4 | header size (`256`) |
| 12 | 4 | entry size (`144`) |
| 16 | 4 | número de arquivos (`1..64`) |
| 20 | 4 | offset da tabela (`256`) |
| 24 | 4 | offset do payload |
| 28 | 4 | tamanho total |
| 32 | 4 | CRC32 da tabela |
| 36 | 4 | CRC32 do header com este campo zerado |
| 40 | 16 | title ID zero-terminated |
| 56 | 80 | título zero-terminated |
| 136 | 16 | versão textual zero-terminated |

`TITLEID` usa 3–15 caracteres ASCII maiúsculos, dígitos, `_` ou `-`.

### TPKG v1: bytes 152..255

| Offset | Size | Campo |
|---:|---:|---|
| 152 | 104 | caminho relativo do ELF de entrada |

v1 é sempre `external-elf` e não exige ícone, preservando compatibilidade com
pacotes já existentes.

### TPKG v2: bytes 152..255

| Offset | Size | Campo |
|---:|---:|---|
| 152 | 96 | caminho relativo do entrypoint |
| 248 | 4 | app type: `0=external-elf`, `1=titan-native`, `2=system-browser`, `3=theme`, `4=titan-game` |
| 252 | 4 | Titan App API (`1` para native; `0` para ELF) |

TPKG v2 exige `icon.rgb565` para apps; pacotes `theme` usam `theme.cfg` e podem usar `preview.rgb565` em vez de uma Home tile.

## File table

Cada entrada possui 144 bytes:

| Offset | Size | Campo |
|---:|---:|---|
| 0 | 128 | caminho POSIX relativo zero-terminated |
| 128 | 4 | offset relativo ao início do payload |
| 132 | 4 | tamanho |
| 136 | 4 | CRC32 do arquivo |
| 140 | 4 | flags, atualmente `0` |

Entradas devem cobrir o payload uma vez, em ordem e sem gaps/overlaps. São
rejeitados caminhos absolutos, prefixos de dispositivo, backslashes, caracteres
de controle, segmentos vazios, `.` e `..`, além de nomes duplicados.

Tamanho máximo do pacote: **256 MiB**.

## Assets reservados v2

```text
icon.rgb565         obrigatório, 64x64 RGB565 LE = 8192 bytes
background.rgb565   opcional, 512x256 RGB565 LE = 262144 bytes
titan_meta.ini      opcional/gerado pelo builder
```

`titan_meta.ini` contém atualmente:

```ini
author=Nome
description=Descricao curta
```

O instalador não confia em um `titan_app.ini` vindo do pacote; ele gera o
manifesto instalado depois de validar entrypoint, checksums, API e assets.

## Manifesto instalado v2

Exemplo:

```ini
format=2
title_id=MYAPP001
title=Meu App
version=1.0.0
type=titan-native
entrypoint=my_app.tmod
api=1
author=Seu Nome
description=Descricao
icon=icon.rgb565
background=background.rgb565
```

`background=` só aparece quando o pacote contém o arquivo.

## Builder

### Titan-native

```sh
python3 kernel/tools/titan_pkg.py build \
  --source ./my_app \
  --output MYAPP001.pkg \
  --title-id MYAPP001 \
  --title "Meu App" \
  --version 1.0.0 \
  --type titan-native \
  --entrypoint my_app.tmod \
  --author "Seu Nome" \
  --description "Descricao"
```

### Jogo nativo TGR

```text
python3 kernel/tools/titan_pkg.py build \
  --source ./my_game \
  --output MYGAME001.tpkg \
  --title-id MYGAME001 \
  --title "Meu jogo" \
  --version 1.0.0 \
  --type titan-game \
  --entrypoint my_game.tmod \
  --author "Seu Nome" \
  --description "Port nativo TGR"
```

`titan-game` é o único tipo que pode chamar `game_begin()`. Consulte
`docs/TITAN_GAME_SDK.md`; ROMs e assets comerciais devem ser fornecidos pelo
próprio usuário e nunca incluídos no SDK público.

### ELF v2 com tile própria

```sh
python3 kernel/tools/titan_pkg.py build \
  --source ./elf_app \
  --output ELFAPP01.pkg \
  --title-id ELFAPP01 \
  --title "ELF App" \
  --version 1.0.0 \
  --type elf \
  --format 2 \
  --entrypoint app.elf
```

### ELF legado v1

```sh
python3 kernel/tools/titan_pkg.py build \
  --source ./legacy \
  --output LEGACY01.pkg \
  --title-id LEGACY01 \
  --title "Legacy ELF" \
  --version 1.0.0 \
  --executable app.elf
```

Verificação:

```sh
python3 kernel/tools/titan_pkg.py verify MYAPP001.pkg
```


## Pacotes de tema `.tthm`

Temas são TPKG v2 com `app type=3`, entrypoint reservado `@theme` e extensão `.tthm`.
Eles permanecem declarativos: `.elf`, `.tmod`, `.irx`, `.so` e `.vsm` são proibidos.
O `title-id` do header deve corresponder exatamente ao `id=` em `theme.cfg`.

Theme Shell API v3 (Build 36) suporta `scene-v2` com HOME/TOP, até seis keyframes
de transição e loops idle HOME/TOP com orçamento compartilhado de oito frames
adicionais. Consulte `docs/THEME_AUTHORING.md` para o manifesto completo, formatos de
asset, orçamento de RAM e comandos de build/verify.

Exemplo:

```sh
python3 kernel/tools/titan_pkg.py build \
  --source ./MYTHEME \
  --output MYTHEME.tthm \
  --title-id MYTHEME \
  --title "My Theme" \
  --version 1.0.0 \
  --type theme
```

## Instalação no PS2

O fluxo da UI é:

```text
★ Debug Settings
  > Package Installer
      > MYAPP001.pkg
          > tela dedicada de instalação
              nome do .pkg
              app / title id / tipo / versão
              barra e percentual
              estado atual
```

A extração é incremental e validada por CRC. Cancelamento ou falha remove o
staging e não publica tile incompleta.

Consulte `docs/TITAN_APP_SDK.md` para a ABI dos apps que rodam dentro da shell.
