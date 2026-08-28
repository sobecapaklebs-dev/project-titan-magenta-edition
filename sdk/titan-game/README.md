# Titan Game SDK v2

O Titan Game Runtime (TGR) permite portar jogos nativos para o Project Titan
sem `ExecPS2`, sem reiniciar o IOP e sem abandonar a shell. O jogo continua no
mesmo processo do Titan e recebe serviços explícitos de vídeo, áudio, input,
rumble, storage e heap.

Este diretório é público e não contém ROMs, assets extraídos de jogos nem
binários proprietários. Um port que depende de dados de um jogo comercial deve
pedir que cada usuário forneça seu próprio dump e deve validar esse dump antes
de montar o TPKG privado.

Arquivos principais:

- `../../kernel/include/titan_app_sdk.h`: ABI do módulo `.tmod` e handoff
  `game_begin/game_end`;
- `../../kernel/include/titan_game_sdk.h`: ABI TGR v2 autoritativa;
- `ABI_REFERENCE.md`: contrato de execução e serviços;
- `PORTING_GUIDE.md`: roteiro de um port e regras de memória/storage;
- `examples/minimal_game.c`: módulo mínimo executado inteiramente dentro do
  Titan;
- `examples/Makefile`: compilação direta com PS2DEV, sem script auxiliar.

O nome da estrutura permanece `TitanGameApiV1` porque a ABI é append-only. O
valor `TITAN_GAME_API_VERSION` é 2 e os campos adicionados devem ser usados
somente depois de verificar `struct_size` e o feature flag correspondente.

## Resultado do runtime

Uma sessão padrão oferece:

- 640x448, dois framebuffers e Z16;
- quatro slots lógicos de controle e rumble DualShock;
- PCM S16 estéreo fixo em 32 kHz;
- heap padrão de 12 MiB, contabilizado e recuperado pelo Titan;
- handles de storage com offsets de 64 bits;
- triângulos coloridos, texturas PSM16 e RGBA32/PSM32 sob demanda, clip space com W, STQ em perspectiva,
  duas texturas em passes ordenados, fog, depth, blend, alpha compare,
  viewport, scissor e wrap/clamp/mirror.

A arena GS de texturas é única e bounded (1 MiB). Handles possuem geração e
releases feitos durante um frame só liberam o bloco após o `FINISH` daquele
packet, impedindo que eviction sobrescreva draws ainda pendentes.

## Compilação do exemplo

Com `mips64r5900el-ps2-elf-gcc` e `mips64r5900el-ps2-elf-ld` no `PATH`:

```text
make -C sdk/titan-game/examples
```

O alvo gera `sdk/titan-game/examples/build/minimal_game.tmod` e valida ISA,
ABI O32, formato ET_REL e relocações aceitas pelo loader do Titan.

O hardware final continua sendo a autoridade para apresentação GS, latência de
áudio, pressão real de memória e comportamento dos controles.

## BuildFix22 graphics/performance capabilities

TGR remains the public rendering interface. The current Titan implementation
may internally route eligible resource work through **pikaVR**, which manages
texture residency/deduplication, exact creation-time PSM8+CLUT selection,
sampler-state caching and auditable VRAM counters. This does not add a public
pikaVR ABI: games must continue to target the versioned TGR contract and must
not depend on internal pikaVR symbols.

**VlombisCore** likewise represents runtime-measured system coordination, not a
flag a game can claim for branding. BuildFix22 only exposes the badge when an
active capability has recorded actual participation. See
`../../docs/PIKAVR_VLOMBISCORE.md`.
