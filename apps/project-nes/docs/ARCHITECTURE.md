# Project Nes — arquitetura Titan-native

## Fluxo de execução

```text
Project Titan HOME
   ↓
Project Nes tile
   ↓
TitanAppRuntime carrega project_nes.tmod
   ↓
titan_app_init(TitanAppApiV1)
   ↓
Project Nes Library
   ↓ X
Carrega iNES do data/ do aplicativo
   ↓
6502 → bus → PPU/APU/controller
   ↓
frame NES 256x240
   ↓
conversão para superfície RGB565 512x256
   ↓
Titan media_present_rgb565 / media_draw

APU aproximada
   ↓
PCM stereo 32 kHz
   ↓
Titan media_submit_pcm
```

O app nunca toma posse direta do GS, framebuffer, AUDSRV ou IOP. A shell continua dona dos recursos e
o módulo usa somente a ABI pública fornecida em `TitanAppApiV1`.

## Vídeo

O framebuffer lógico NES é 256x240. Para respeitar o contrato atual da Titan media API, Project Nes
monta uma superfície 512x256. A imagem NES é reamostrada horizontalmente para 320x240 e centralizada,
deixando pequenas bordas dentro da superfície. O Titan então apresenta essa superfície pelo renderer
existente, inclusive no modo 720p sem o app criar sua própria superfície GS.

## CPU

A preview implementa os 151 opcodes oficiais do MOS 6502. Opcodes ilegais não são executados: se uma
ROM depender deles, o core interrompe a sessão e retorna uma mensagem de incompatibilidade em vez de
continuar com estado indefinido.

## PPU

A PPU 0.1 é orientada a frame, não a ciclo. Implementa registradores essenciais, nametables, palette RAM,
CHR, mirroring, OAM, DMA, background e sprites 8x8. Isso é suficiente para a ROM de diagnóstico e para
experimentar homebrew/NROM simples, mas jogos com efeitos raster/cycle timing podem falhar.

## Áudio

A preview interpreta os registradores principais de pulse 1, pulse 2 e triangle e gera uma aproximação
PCM em 32 kHz. Envelope, sweep, length-counter preciso, noise e DMC serão etapas posteriores. O objetivo
inicial é validar a continuidade de áudio da Titan media API dentro de um emulador real.

## Input e saída

`Select + Círculo` solicita `request_exit()`. O próprio kernel Titan também reserva esse combo como escape
de emergência para Titan Apps, então o comportamento é consistente com a plataforma.


### BuildFix27 CP46 — FCEUmm libretro adapter

A link-gated Titan-side adapter now maps the frozen FCEUmm libretro ABI into `ProjectNesCoreBackend`. The adapter owns libretro callbacks, RGB565 frame handoff, input translation, PCM capture, save-RAM dirty tracking and deterministic `retro_unload_game`/`retro_deinit` teardown. It does not call Titan services directly. The packaged/default build remains Legacy NROM until the exact frozen GPL source archive is materialized and target-linked.
