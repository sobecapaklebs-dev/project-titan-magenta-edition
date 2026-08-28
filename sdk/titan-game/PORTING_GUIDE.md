# Guia de port para o Titan Game Runtime

## 1. Separe código e dados comerciais

Mantenha o repositório público sem ROM, ISO, firmware, áudio, texturas ou assets
extraídos. O builder privado deve receber o dump do usuário, validar tamanho,
header/região e hash conhecido, e só então criar o TPKG local.

O pacote público pode conter patches, adaptadores, tabelas de offsets não
derivadas e documentação. Não publique o TPKG privado resultante.

## 2. Preserve a lógica do jogo

Porte a simulação nativa em C/C++. Substitua apenas a fronteira da plataforma:

- janela/GPU -> Titan Game Video;
- SDL/input -> quatro pads TGR;
- device áudio -> fila PCM 32 kHz;
- `fopen`/FileXio direto -> handles TGR;
- threads de frontend -> trabalho cooperativo por frame;
- rumble -> `set_pad_rumble`;
- `exit`, abort e loops fatais -> estado de erro + saída cooperativa.

Não use `ExecPS2` como atalho. Um port TGR deve retornar à HOME por
`game_end()` sem reinicializar a shell.

## 3. Faça o orçamento antes de ligar tudo

Use memória bounded. Um desenho adequado para jogos baseados em ROM é:

- pool principal fixo;
- cache paginado para ROM, sem carregar o dump inteiro;
- cache LRU para áudio decodificado, protegendo vozes ativas;
- arena de texturas com eviction explícito;
- filas de render com flush antecipado em vez de buffers gigantes;
- sem framebuffers duplicados na BSS: o GS é dono das superfícies reais.

Teste OOM em cada grupo. A falha deve fechar áudio/storage/vídeo e voltar ao
Titan, nunca continuar com ponteiro nulo.

## 4. Traduza o renderer por estado

Para engines N64-era, preserve clip-W e gere S/T/Q; converter tudo para XY já
dividido perde correção de perspectiva. Mapeie explicitamente combiner, fog,
alpha compare, depth compare/write, blend, viewport, scissor e sampler.

Mantenha S/T normalizados e envie Q=1/W; TGR cuida da razão NPOT/POT do TEX0.
Não reutilize um handle depois de `video_release_texture()`. Texturas com alpha
apenas 0/255 devem ficar em PSM16; quando houver alpha intermediário, negocie
`TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32` e use o callback RGBA32 append-only.

Como o GS possui uma unidade de textura, materiais de duas texturas precisam de
passes ordenados. Valide separadamente menus, céus, água, armas, decals,
transparências e efeitos aditivos.

## 5. Compile como R5900 ET_REL

Flags mínimas:

```text
-D_EE -G0 -Os -std=gnu11 -march=r5900 -mabi=32 -mhard-float
-msingle-float -ffreestanding -fno-common -fno-pic -mno-abicalls
-fno-stack-protector
```

Compile cada unidade com `mips64r5900el-ps2-elf-gcc -c` e faça o link parcial:

```text
mips64r5900el-ps2-elf-gcc -print-libgcc-file-name
mips64r5900el-ps2-elf-ld -r --no-relax -o game.tmod objetos... /caminho/retornado/libgcc.a
```

O loader aceita `R_MIPS_32`, `R_MIPS_26`, `R_MIPS_HI16` e `R_MIPS_LO16`.
Valide com `apps/project-nes/tools/check_r5900_tmod.py` e faça uma segunda
auditoria de imports específica do port. O `examples/Makefile` automatiza essas
duas linhas e pode ser chamado diretamente de Bash, Fish ou outro shell.

## 6. Monte um TPKG privado

Estrutura:

```text
package/
|-- game.tmod
|-- icon.rgb565
|-- rom/
|   `-- dump-do-usuario.z64
`-- data/
```

Build:

```text
python3 kernel/tools/titan_pkg.py build --source package --output GAME0001.tpkg --title-id GAME0001 --title "Meu jogo" --version 1.0.0 --type titan-game --entrypoint game.tmod --author "Autor" --description "Port nativo TGR"
python3 kernel/tools/titan_pkg.py verify GAME0001.tpkg
```

O instalador exige DEX Mode somente para instalar. Depois da instalação, o jogo
aparece na HOME e roda sem sair do Project Titan.

## 7. Matriz mínima de validação

Antes de chamar um port de pronto, valide:

- load/unload repetido e retorno limpo à HOME;
- ROM ausente, ROM errada e leitura além do EOF;
- OOM do pool, texturas e áudio;
- desconexão/reconexão dos pads e motores zerados na saída;
- filas de áudio cheias/vazias e música/SFX simultâneos;
- viewport/scissor, Z, alpha, fog, dois ciclos e duas texturas;
- split-screen em meia tela e quadrantes, inclusive caminhos indexed/clip/material;
- churn de textura até pressão total, release no meio do frame e alpha graduado;
- save create/truncate/rename, queda durante escrita e path traversal;
- campanha, menus, pause, multiplayer e cinematics;
- hardware PS2 real, além de PCSX2.

Os testes host detectam violações de contrato e regressões determinísticas; não
substituem validação de GS, SPU2, IOP e timing no console.
