# Titan Game SDK v2

Jogos nativos que permanecem dentro do Project Titan usam TPKG v2
`type=titan-game`, o handoff package-gated `game_begin/game_end` e a ABI pública
de `kernel/include/titan_game_sdk.h`.

A documentação, exemplo mínimo e guia de port estão em:

```text
sdk/titan-game/
```

O SDK cobre vídeo GS 640x448/Z16, arena de texturas PSM16/PSM32 com lifetime
seguro, materiais de duas texturas com STQ, fog, depth/blend/alpha,
viewport/split-screen, quatro pads, rumble, áudio PCM 32 kHz, storage 64-bit
sandboxed e heap padrão de 12 MiB. A auditoria matemática do renderer está em
`docs/TGR_SM64_PS2_AUDIT.md`. O repositório público nunca deve incluir ROM ou
assets extraídos; cada usuário monta localmente seu pacote privado com dados
que possui legitimamente.

## BuildFix22: optimization layer status

The public renderer contract remains TGR. BuildFix22 contains an internal
pikaVR resource-management layer for residency, deduplication, exact
palettization/CLUT fallback, sampler-state caching and GS telemetry. These are
implementation details and do not freeze a public pikaVR ABI.

VlombisCore capabilities are also runtime-measured. A badge is not granted by
title or package metadata alone. See `docs/PIKAVR_VLOMBISCORE.md` for the
current HOST-VALIDATED engineering status and validation limits.
