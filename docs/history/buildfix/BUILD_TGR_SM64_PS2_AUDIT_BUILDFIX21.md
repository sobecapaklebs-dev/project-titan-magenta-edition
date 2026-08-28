# BuildFix21 — TGR / SM64 PS2 renderer audit

Data: 2026-08-19

## Escopo concluído

- arquitetura, boot chain e ownership Titan preservados;
- backend PS2 do SM64 usado somente como referência comportamental;
- 32 alocações GS independentes substituídas por uma arena única de 1 MiB;
- subalocador mixed PSM16/PSM32 com blocos de 256 bytes;
- handles com geração, estado retired e reuse apenas após GS `FINISH`;
- fence `FINISH` obrigatório mesmo quando o packet corrente está vazio;
- IDs lógicos GoldenEye reutilizáveis (2048 entradas);
- RGBA32/PSM32 append-only para alpha intermediário, com fallback PSM16;
- TEXA e conversão pública 0..255 para GS 0..128 explícitas;
- viewport parcial/split-screen unificado nos caminhos indexed, clip e material;
- ST/Q corrigido para coordenada normalizada, Q e dimensão TEX0 POT/NPOT;
- packing GIF `PACKED` de ST, RGBAQ e XYZ2 validado bit a bit;
- documentação pública do SDK e contrato de regressão atualizados.

Detalhes e fórmulas: `docs/TGR_SM64_PS2_AUDIT.md`.

## Validação host

`bash kernel/tests/run_host_tests.sh` terminou com exit code 0. A suíte cobriu
release, VRAM 480i/720p, loader/installer, storage, temas/Ribbon, TGR, bridge
TitanEYE, ROM z64/v64/n64 e os novos testes de renderer.

Testes focados aprovados sob ASan/UBSan:

```text
titan-game-runtime: OK
titan-game-renderer: OK texture lifetime, alpha, viewport, ST/Q/GS packing
titan-ge-platform: OK RGBA32 preservation with PSM16 fallback
titan-game-runtime-contract: OK
titaneye-port-contract: OK source, lifecycle, memory, branding, SDK, private ROM gate
```

Auditoria estrutural do módulo TitanEYE:

```text
sources=222
text=1554164
data=234528
bss=4276728
total=6065420
limit=8388608
headroom=2323188
```

## Gate externo não falsificado

Este ambiente não possui `mips64r5900el-ps2-elf-gcc`/`ld`, PCSX2 nem acesso a
um PS2 físico. Portanto nenhum `.tmod` R5900/TPKG executável novo foi inventado.
Cross-build, inspeção das relocações reais e validação visual/timing de
GS/SPU2/input permanecem gates obrigatórios no ambiente PS2DEV/hardware.
