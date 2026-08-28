# Auditoria TGR contra o backend PS2 do SM64

Referência comportamental: `gfx_ps2_rapi.c` do branch `ps2` de
[`fgsfdsfgs/sm64-port`](https://github.com/fgsfdsfgs/sm64-port/tree/ps2), fixado
no commit `5b693af3d1de703697edc91439c8393a54a66763`. As unidades e macros de VRAM,
TEX0, transferência e GIF foram conferidas também no
[`ps2dev/ps2sdk`](https://github.com/ps2dev/ps2sdk).

O código do SM64 não substitui a arquitetura do Titan. TGR continua dono da
sessão foreground; `TitanVideo` continua dono dos framebuffers, do packet PATH3,
da alocação de VRAM e da restauração da HOME. A auditoria incorporou somente os
invariantes de GS e as regressões que faltavam.

## Resultado por risco

| Área | Falha encontrada | Correção no Titan |
| --- | --- | --- |
| Pressão de texturas | 32 alocações individuais excediam a tabela global de 16 alocações de VRAM, já parcialmente ocupada por framebuffer/Z | Uma única arena GS de 1 MiB, subalocada em blocos de 256 bytes |
| Lifetime | Um índice liberado podia ser reutilizado enquanto um packet GIF ainda o referenciava | Handle com geração e estados `LIVE -> RETIRED -> FREE`; reuse só depois de `FINISH`, inclusive no flush antecipado por pressão |
| IDs do port | IDs lógicos cresciam até esgotar o espaço mesmo após eviction | Tabela live de 2048 IDs reutilizáveis e release separado do handle backend |
| Layout GS | Largura lógica era usada como TBW e dimensão bruta era usada onde TEX0 exige expoente | TBW físico de 64/128 pixels; `TW/TH=ceil(log2(dimensão lógica))`; linhas de upload são preenchidas até TBW |
| Alpha | RGBA32 era sempre reduzido a A1; TEXA não era definido; AREF público era aplicado na escala errada | PSM16 permanece para alpha binário, PSM32 é usado para alpha intermediário, TEXA é explícito e alpha/AREF são convertidos de 0..255 para 0..128 |
| Viewport | O caminho material respeitava viewport parcial, mas indexed e clip-space ainda mapeavam para a tela inteira | Os três caminhos usam a mesma função validada de clip para viewport; scissor é normalizado uma vez |
| ST/Q | Coordenadas normalizadas eram multiplicadas pela largura e o packing não tinha teste de lane | ST considera Q e a dimensão POT declarada; testes verificam bits/lane de ST, RGBAQ e XYZ2 |

## Arena e lifetime de texturas

A arena ocupa exatamente 1.048.576 bytes e usa uma única entrada do alocador
global. Para largura lógica `W` e altura `H`:

```text
TBWpx = 64, se W <= 64; caso contrário, 128
Hp    = round_up(H, 8)
bytes(PSM16) = TBWpx * Hp * 2
bytes(PSM32) = TBWpx * Hp * 4
```

Cada subalocação começa em limite de 256 bytes, exigido pelo bloco GS. O
endereço retornado pelo alocador PS2 está em palavras de 32 bits; portanto o
endereço usado por upload/TEX0 é `arena_base_words + offset_bytes / 4`. O TBW
enviado ao GS é `TBWpx / 64` através de `draw_texturebuffer()`.

Existem 1024 descritores EE, mas a capacidade real é sempre limitada pelos
bytes da arena: por exemplo, cabem 32 texturas 128x128 PSM16 ou 16 PSM32, além
de combinações menores. Um release durante frame torna o descritor `RETIRED` e
invalida imediatamente o handle; os blocos só voltam ao bitmap depois que o GS
confirma `FINISH`. Sob pressão, TGR fecha o prefixo corrente, espera `FINISH`,
recupera os aposentados e tenta uma única vez novamente.
Uma barreira solicitada nunca é omitida quando o packet corrente está vazio:
ela também cerca packets anteriores enviados por flush de capacidade sem
`FINISH`.

## Alpha

O domínio público continua 0..255. A conversão exata usada por vértices,
texturas PSM32 e AREF é:

```text
A_gs = floor((A_public * 128 + 127) / 255)
```

Para texturas que só contêm alpha 0/255, o bridge usa RGBA5551/PSM16. TGR
programa `TEXA(TA0=0, AEM=0, TA1=128)`, de modo que o bit A1 resulte em alpha
GS 0/128 sem tornar RGB preto acidentalmente transparente. Se qualquer texel
tiver alpha intermediário, o bridge usa a extensão append-only
`video_upload_texture_rgba32`, preservando a gradação em PSM32. Alpha compare
usa `GEQUAL`; por exemplo, o limiar público 128 vira AREF 64.

## Viewport e split-screen

Depois de `clip / w`, todos os caminhos aplicam:

```text
screen_x = viewport_x + (ndc_x * 0.5 + 0.5) * viewport_width
screen_y = viewport_y + (0.5 - ndc_y * 0.5) * viewport_height
screen_z = round(clamp((1 - ndc_z) * 0.5, 0, 1) * 65535)
```

O backend Titan declara clip Z em -1..1 (`z_is_from_0_to_1=false`), por isso o
fator 0,5 é intencional; o backend SM64 declara 0..1 e usa outra forma
equivalente para o seu domínio. Viewports de quadrante e meia-tela são cobertos
por testes tanto no lane material quanto nos lanes indexed e clip-space.

## ST/Q e packing GIF

Para coordenada normalizada `u`, dimensão lógica `W`, expoente TEX0 `TW` e
`q=1/w`:

```text
declared_width = 2^TW = next_power_of_two(W)
S = u * (W / declared_width) * q
```

O GS interpola S/T/Q e amostra `S/Q`; multiplicar pela razão lógica/POT faz a
borda lógica continuar em `u*W`, inclusive para NPOT. O mesmo vale para T.

No modo GIF `PACKED`, cada registro ocupa um qword. TGR valida exatamente:

```text
ST:    [float_bits(S), float_bits(T), float_bits(Q), 0]
RGBAQ: [R, G, B, A_gs]                 (um componente por lane de 32 bits)
XYZ2:  [round((X+2048)*16), round((Y+2048)*16), Z, 0]
```

A ordem do vertex é `ST -> RGBAQ -> XYZ2`, necessária para o GS capturar Q do
payload ST antes de consumir RGBAQ. Saturação protege o formato XY 12.4 contra
NaN e overflow.

## Limite da validação host

Os contratos host verificam ABI append-only, allocator, geração de handles,
deferred reuse, fórmulas de viewport/ST/Q, packing bit a bit, alpha e integração
do bridge. Eles não substituem o cross-build R5900 nem a validação visual e de
timing em PCSX2/PS2 real.
