# Referência da ABI Titan Game v2

## Ciclo de vida

Um jogo é distribuído como TPKG v2 `type=titan-game`. O entrypoint é um objeto
ELF32 MIPS `ET_REL`, O32, R5900, não-PIC, com os mesmos exports de um Titan App:

```c
int titan_app_init(const TitanAppApiV1 *api, const char *app_root);
void titan_app_update(const TitanAppInput *input, uint32_t delta_ms);
void titan_app_draw(void);
void titan_app_shutdown(void); /* opcional */
```

`titan_app_init()` é apenas o handshake: valide e guarde os ponteiros, mas não
chame serviços. No primeiro `titan_app_update()`, configure
`TitanGameSessionConfig` e chame `TitanAppApiV1.game_begin()`. Quando o jogo
terminar, chame `game_end()` exatamente uma vez e então `request_exit()` para
voltar à HOME. `titan_app_shutdown()` deve tolerar uma chamada mesmo após falha
parcial e concluir qualquer cleanup ainda pendente.

O handoff só funciona para um pacote instalado como `titan-game`. O mesmo
módulo renomeado para `titan-native` recebe falha em `game_begin()`.

## Negociação

O port deve verificar:

1. `TitanAppApiV1.abi_version == TITAN_APP_API_VERSION`;
2. `TitanAppApiV1.struct_size` cobre `game_end`;
3. `TitanGameApiV1.abi_version == TITAN_GAME_API_VERSION`;
4. `TitanGameApiV1.struct_size` cobre o último callback que será usado;
5. todos os bits necessários em `feature_flags`.

Nunca desreferencie um campo append-only sem testar o tamanho da estrutura.

## Vídeo

| Recurso | Contrato |
| --- | --- |
| Superfície | 640x448, double buffer, Z16 |
| Frame | `video_begin_frame` -> submits -> `video_end_frame` |
| Textura | RGB5551/PSM16 para alpha binário; RGBA32/PSM32 opcional para alpha intermediário; libere o handle |
| 3D básico | indexed triangles com MVP row-major |
| Fast3D/GBI | clip-space X/Y/Z/W ou material vertices com S/T/Q |
| Material | shade, tex0, modulate, decal e tex0/tex1 LOD |
| Estado | viewport, scissor, depth, blend, alpha compare, fog e samplers |

O GS possui uma unidade de textura. O modo
`TITAN_GAME_COMBINE_TEX0_TEX1_LOD` é executado pelo Titan como dois passes
ordenados. `q0` e `q1` são o recíproco de clip-W; use 1.0 somente para mapeamento
afim.

Texturas são recursos limitados. Um port deve manter cache próprio, tratar
falha de upload e chamar `video_release_texture()` durante eviction e shutdown.
Handles carregam uma geração e ficam inválidos assim que liberados. O espaço
GS só é reutilizado depois que os draws pendentes terminam; não retenha nem
compare handles antigos.

`video_upload_texture_rgba32()` foi anexado ao fim da estrutura v2 e exige
`TITAN_GAME_FEATURE_TEXTURE_UPLOAD_RGBA32` mais `struct_size` suficiente. Os
bytes são R,G,B,A, com stride em bytes. Use-o apenas quando a textura realmente
precisar de alpha intermediário; PSM16 consome metade da arena.

`video_upload_texture_i8()` é a extensão append-only usada pela lane nativa de
textura indexada/intensidade. Ela exige `TITAN_GAME_FEATURE_TEXTURE_UPLOAD_I8`
e `struct_size` suficiente para cobrir o callback. Cada byte I8 é preservado
como índice PSM8 com CLUT grayscale; o stride também é expresso em bytes.
Ports devem manter fallback quando o bit/callback não estiver disponível.

S/T são normalizados e pré-divisão. Para perspectiva, envie Q=1/W. TGR corrige
automaticamente a razão entre a dimensão lógica e o tamanho potência de dois
declarado no TEX0; não multiplique S/T pela largura ou altura em texels.

## Input e haptics

`poll_input()` atualiza quatro slots estáveis. `get_pad()` devolve held,
pressed, released, estado de conexão e dois analógicos. A implementação física
pode iniciar com duas portas e crescer para multitap sem alterar a ABI.

`set_pad_rumble(index, small, large)` é opcional e exige
`TITAN_GAME_FEATURE_HAPTICS`. Pare os motores antes de encerrar a sessão.

## Áudio

O formato é sempre PCM S16LE, dois canais, 32000 Hz. O produtor deve consultar
`audio_free_frames()` e alimentar a fila sem bloquear a thread de frame. Chame
`audio_begin()` uma vez e `audio_end()` no cleanup.

## Heap

O heap padrão é 12 MiB. `alloc()` aceita alinhamentos potência de dois entre 16
e 4096. Toda alocação é contabilizada, e blocos restantes são recuperados ao
fim da sessão; isso é proteção de lifecycle, não substitui eviction e budgeting
corretos.

O arquivo `.tmod` e sua imagem relocada possuem teto de 8 MiB cada. A imagem do
módulo não faz parte do heap de jogo, mas ambas consomem a RAM física do EE.
Planeje os dois orçamentos juntos.

## Storage

Leituras ficam abaixo da raiz instalada do pacote. Escritas são aceitas apenas
em `data/`. Caminhos com `..`, `.`, backslash, prefixo de dispositivo ou escape
da raiz são rejeitados. Handles e seek usam offsets de 64 bits.

Durante a sessão o TGR é o único dono do lane FileXio. Evite reads longos por
frame: use páginas pequenas, cache bounded e yield natural entre frames.

## Imports do módulo

O loader não oferece resolução arbitrária de símbolos. Jogos recebem um
allowlist fechado de funções puras de `libc/libm` para string, parsing,
formatação e matemática. Não existem imports diretos de arquivo, dispositivo,
processo, rede, thread ou alocação. Essas capacidades passam pelas tabelas
Titan.

Audite sempre `nm -u` no `.tmod`. Qualquer símbolo fora do allowlist fará o
load falhar de forma fechada.
