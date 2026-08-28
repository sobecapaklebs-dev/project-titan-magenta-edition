# Project Nes — roadmap sugerido

## 0.1 — hardware proof

- TPKG v2 Titan-native;
- ROM de teste embutida;
- NROM;
- 6502 oficial;
- PPU frame-based;
- pulse/triangle aproximados;
- retorno com Select + Círculo.

## 0.2 — compatibilidade NES básica

- SRAM persistente em `data/saves/`;
- APU: envelope, sweep, length, noise;
- mapper 2 (UxROM), mapper 3 (CNROM), mapper 7 (AxROM);
- identificação de região NTSC/PAL;
- opções de scaling e overscan sem alterar o tema do Titan.

## 0.3 — biblioteca real

A evolução ideal da Titan App API é adicionar uma API read-only de enumeração de diretórios/ROMs para
Titan Apps. Isso remove o slot fixo `game.nes` e permite uma biblioteca em `mass:/TITAN/roms/nes/`.

## 0.4+

- MMC1 e MMC3;
- save states;
- Famicom/Dendy;
- melhorias de timing da PPU;
- A/V sync refinado;
- metadados/capas opcionais usando serviços Titan.
