# Project Nes 0.1.5 Preview

**Project Nes** é um Titan App nativo (`titan-native`) para o Project Titan Magenta Edition.
Ele executa dentro da shell pela Titan App API: não usa `ExecPS2`, não abandona o Project Titan
e retorna diretamente à HOME quando o usuário sai.

> **A powerfull NES emulator for Project Titan.**  
> **Powered by: VlombisCore(TM) Technology**

Esta primeira preview foi criada para validar em hardware real o caminho completo de um app
complexo: Package Installer → `.tmod` → CPU emulada → PPU → vídeo RGB565 → áudio PCM → input → retorno à shell.

## Teste imediato

O `PROJECTNES.pkg` já contém `ProjectNesTest.nes`, uma ROM homebrew de diagnóstico criada para
o projeto. Ela mostra `PROJECT NES / READY NOW`, alterna a cor por NMI e gera um tom de teste.
Nenhuma ROM comercial ou dado de jogo da Nintendo está incluído.

Instale pelo fluxo normal:

```text
DEX Mode: Enabled
  → ★ Debug Settings
  → Package Installer
  → PROJECTNES.pkg
  → Install
  → HOME
  → Project Nes
```

Dentro do Project Nes, deixe `ProjectNesTest.nes` selecionada e pressione **X**.

## Controles

| DualShock 2 | NES / Project Nes |
|---|---|
| D-pad | D-pad NES |
| X | A |
| Círculo | B |
| Start | Start |
| Select | Select |
| Triângulo | Pausar / continuar |
| Quadrado | Reset da ROM |
| **Select + Círculo** | **Sair para o Project Titan** |

O combo **Select + Círculo** é o escape padrão do app, conforme a convenção definida para Titan Apps.
O Project Nes abre na janela padrão da Titan App Platform; **R3** é reservado pelo sistema para alternar entre janela e tela cheia.

## ROM externa para teste

A Titan App API v1 ainda não expõe enumeração genérica de diretórios. Por isso a preview possui um
slot externo simples. Depois de instalar o app, coloque uma ROM Mapper 0 com o nome:

```text
mass:/TITAN/apps/PROJECTNES/data/roms/game.nes
```

Reabra o Project Nes. `game.nes` aparecerá na biblioteca e pode ser escolhida com o D-pad.

Use apenas dumps que você tenha direito de usar ou homebrew livre.

## Escopo técnico desta preview

- iNES clássico;
- Mapper 0 / NROM;
- PRG-ROM de 16 KiB ou 32 KiB;
- CHR-ROM de 8 KiB ou CHR-RAM;
- conjunto completo de **opcodes oficiais 6502**;
- PPU por frame com background, atributos, paleta e sprites 8x8;
- mirroring horizontal e vertical;
- OAM DMA;
- controle NES 1P;
- saída de vídeo pela `media_present_rgb565` da Titan App API;
- APU inicial aproximada: pulse 1, pulse 2 e triangle em PCM S16LE/32 kHz;
- pausa, reset e retorno sem sair da shell.

Não considere 0.1.5 um substituto de emuladores NES maduros ainda. A PPU não é cycle-accurate,
e APU envelope/sweep/noise/DMC, mappers adicionais, SRAM persistente e save states ficam para as
próximas revisões. O objetivo desta build é provar a arquitetura Titan-native em PS2 real com uma ROM funcional.

## Compilar

Com o PS2DEV no ambiente:

```sh
make
make verify
make host-test
```

Saída:

```text
build/PROJECTNES.pkg
```

Veja `docs/BUILD_AND_INSTALL.md`, `docs/ARCHITECTURE.md` e `docs/TESTING.md`.

## Licença e marca

O código original desta preview e a ROM de diagnóstico incluída são distribuídos sob MIT.
Project Nes é um projeto homebrew não oficial e não possui afiliação com a Nintendo.

## BuildFix27 provider status

The **BuildFix27 release provider is FCEUmm/libretro**, frozen at commit
`b5e3566515c27dc66c9c20572171673126532e06` (Git tree
`6adfb382bf8a369f1a6ee224e89c08d981e22cec`). The exact source is materialized
under `third_party/fceumm/modern` and its imported file hashes are recorded in
`external/fceumm/FCEUMM_IMPORTED_SHA256SUMS`.

The old **Legacy NROM** implementation remains in the source tree only as a
host/recovery fixture used by historical contracts; it is not the BF27 release
provider. The target Makefile compiles the frozen FCEUmm portable-C source list
for the PlayStation 2 Emotion Engine / MIPS R5900 with `STATIC_LINKING=1`,
`HAVE_HDPACK=0`, `HAVE_NTSC=0` and RGB565 frontend support. No ARM/NEON path is
part of the Titan target.

Project NES original integration code remains MIT-licensed. Because the release
module links FCEUmm, the **combined distributed module is GPL-2.0**. The package
includes the MIT component text, the GNU GPL v2 text, and the frozen FCEUmm
source provenance record.

### Titan ↔ FCEUmm adapter

The Titan-side libretro adapter maps FCEUmm into `ProjectNesCoreBackend` and
owns libretro lifecycle, ROM-in-memory loading, RGB565 frame handoff, input,
32 kHz PCM audio, save RAM publication, reset, unload/deinit and deterministic
resource cleanup. Auxiliary read-only core files use the Titan sandbox through
`data/system`; commercial ROM content is never bundled.
