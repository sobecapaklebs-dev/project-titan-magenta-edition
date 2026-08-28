# Titan App SDK v1

Este documento define a primeira ABI pública para aplicativos que executam
**dentro do Project Titan Magenta Edition**, sem `ExecPS2`, sem sair da shell e
sem reinicializar o IOP.

A proposta é permitir que a comunidade escreva ferramentas, clientes de rede,
visualizadores, players e outros aplicativos que reutilizem o renderer, fonte,
notificações, input, áudio e serviços do Project Titan.

> **Importante:** Titan Apps são módulos nativos MIPS executados no mesmo
> processo do kernel/shell. A ABI restringe os serviços que o aplicativo recebe,
> mas isto **não é uma sandbox de segurança de hardware**. Instale código apenas
> de fontes em que você confia. O requisito de DEX para instalar é uma barreira
> de desenvolvimento/distribuição, não isolamento de código malicioso.

## 1. Aplicativos e jogos nativos

O Package Installer aceita estes modelos de execução:

- **ELF externo** — aplicativo tradicional. Ao iniciar, o Titan faz handoff e a
  shell deixa de executar.
- **Titan-native** — módulo `.tmod` que implementa Titan App API v1. É carregado,
  relocacionado e executado dentro do processo do Titan. Ao sair, a HOME volta
  imediatamente, sem novo boot da shell.
- **Titan-game** — módulo `.tmod` confiável que usa o mesmo lifecycle e, após o
  handshake, pode reservar o Titan Game Runtime com `game_begin()`. A ABI TGR
  pública está em `docs/TITAN_GAME_SDK.md` e `sdk/titan-game/`.

Para novos aplicativos integrados ao sistema, use `titan-native`.

## 2. Instalação e DEX

Pacotes ficam em:

```text
mass:/TITAN/packages/*.pkg
```

A instalação é exposta somente em:

```text
★ Debug Settings > Package Installer
```

com **DEX Mode ativado**. A própria camada de serviço também rejeita uma
instalação iniciada sem DEX, portanto ocultar/mostrar a tela não é a única
verificação.

Depois de instalado, o app aparece na grade principal e pode ser executado com
DEX desligado. DEX é necessário para **instalar**, não para usar o aplicativo já
instalado.

O **Performance Overlay** é uma preferência separada do DEX Mode. Em
`★ Debug Settings` ele pode ser desativado e permanecer assim entre boots sem
desligar o DEX; o Package Installer continua disponível enquanto DEX estiver
ativo.

Para remover um app instalado, selecione sua tile na HOME, pressione **Select**
e escolha **Desinstalar**. O diretório inteiro do aplicativo, inclusive
`data/`, é removido após confirmação.

## 3. Estrutura de um Titan App

Exemplo de fonte de pacote:

```text
my_app/
├── my_app.tmod            obrigatório para titan-native
├── icon.rgb565            obrigatório em TPKG v2
├── background.rgb565      opcional
├── data/                   opcional, dados iniciais somente
└── outros arquivos...     opcional
```

O instalador gera `titan_app.ini`; não o coloque no pacote. Autor e descrição
são fornecidos ao builder e viram `titan_meta.ini` dentro do TPKG.

Instalação publicada:

```text
mass:/TITAN/apps/MYAPP001/
├── titan_app.ini
├── my_app.tmod
├── icon.rgb565
├── background.rgb565      se houver
└── data/
```

A publicação é transacional. O diretório temporário `.install-TITLEID` não entra
no catálogo da HOME e só é renomeado para o caminho final depois da validação.

## 4. Formatos visuais do pacote

### `icon.rgb565`

- obrigatório em TPKG v2;
- 64 × 64 pixels;
- RGB565 little-endian;
- exatamente 8192 bytes.

O arquivo permanece 64×64 no pacote. Para não gastar VRAM adicional — sobretudo
em 720p, onde o layout de dois framebuffers já ocupa os 4 MiB do GS — o runtime
filtra o ícone para uma célula 32×32 ainda livre no atlas de avatares existente.
O atlas original não é alterado em disco e os avatares existentes não são
substituídos.

A ABI v1 dispõe de **57 células dinâmicas simultâneas** para ícones de apps
instalados. O pacote continua obrigado a incluir seu ícone; se o catálogo exceder
essa quantidade, apps além da capacidade de atlas continuam instalados e
executáveis, mas usam o placeholder genérico até haver uma célula dinâmica
disponível. Esse limite existe para preservar os 4 MiB de VRAM e o double buffer
720p sem remover nenhum asset residente.

### `background.rgb565`

- opcional;
- 512 × 256 pixels;
- RGB565 little-endian;
- exatamente 262144 bytes.

O fundo é desenhado na área lógica do app e pode ser omitido. Sem ele, o
aplicativo recebe o fundo normal animado da shell.

Para converter uma imagem comum:

```sh
python3 kernel/tools/titan_rgb565.py icon.png icon.rgb565 \
  --width 64 --height 64 --resize

python3 kernel/tools/titan_rgb565.py background.png background.rgb565 \
  --width 512 --height 256 --resize
```

O conversor usa Pillow somente no PC; o PS2 não depende de Pillow.

## 5. Compilando um `.tmod`

Um `.tmod` é um **ELF32 MIPS relocável (`ET_REL`)**, não um ELF executável
independente. Por isso a etapa correta é compilar com `-c` e **não** linkar como
programa PS2 separado.

Exemplo:

```sh
mips64r5900el-ps2-elf-gcc \
  -D_EE -G0 -Os -Wall -Wextra \
  -march=r5900 -mabi=32 \
  -ffreestanding -fno-common -fno-pic -mno-abicalls \
  -Ikernel/include \
  -c app.c -o app.tmod
```

**O alvo R5900 é parte da ABI, não apenas uma recomendação.** Um `.tmod` que
foi compilado para MIPS32/MIPS32r2 genérico pode conter instruções que o EE não
executa. Como Titan-native roda dentro do mesmo processo da shell, isso pode
derrubar o Project Titan inteiro. O runtime e o Package Installer agora exigem
`EF_MIPS_ARCH_3 + EF_MIPS_MACH_5900`, ABI O32 e objeto não-PIC antes de permitir
execução/publicação. Não use `clang --target=mipsel-none-elf` nem outro alvo MIPS
genérico para produzir pacotes de hardware; ele continua útil apenas para testes
estruturais host-side.

O loader v1 suporta as relocações MIPS:

```text
R_MIPS_32
R_MIPS_26
R_MIPS_HI16
R_MIPS_LO16
```

Objetos MIPS modernos normalmente carregam seções de metadata como `.reginfo`,
`.MIPS.options` e `.MIPS.abiflags` marcadas como `SHF_ALLOC`. O loader Titan
reconhece essas seções como metadata de ABI e **não** as copia para a imagem
executável; seções alocáveis desconhecidas continuam sendo rejeitadas. Isso
permite usar toolchains MIPS atuais sem transformar metadata do compilador em
memória de runtime por engano.

Símbolos indefinidos não são resolvidos livremente contra o kernel. A whitelist
v1 contém apenas:

```text
memcpy
memset
memcmp
strlen
strcmp
```

Use a `TitanAppApiV1` para falar com o sistema. Não faça link direto contra
funções privadas do Project Titan ou PS2SDK se você quiser manter compatibilidade
com a ABI pública.

O módulo pode ocupar no máximo 8 MiB no arquivo e a imagem relocada possui teto
de 8 MiB. Titan Apps de UI devem continuar pequenos; o limite ampliado existe
para ports `titan-game` confiáveis e não aumenta o heap normal de um app.

## 6. Exports obrigatórios

Inclua:

```c
#include "titan_app_sdk.h"
```

E exporte:

```c
int titan_app_init(const TitanAppApiV1 *api, const char *app_root);
void titan_app_update(const TitanAppInput *input, uint32_t delta_ms);
void titan_app_draw(void);
void titan_app_shutdown(void); /* opcional */
```

### `titan_app_init`

Chamado uma vez. Retorne `0` para aceitar a sessão. No runtime RC14 BuildFix10,
a inicialização é executada em uma thread EE dedicada enquanto a shell mantém
a tela de lançamento viva. Chamadas iniciais da Titan API que precisam de
storage/alocação/notificação são encaminhadas para o dono principal do runtime.
Existe um watchdog de 15 segundos e `Select + Círculo` pode cancelar a
inicialização antes de o app assumir a superfície. Isso protege contra loops ou
init bloqueante; não transforma código nativo em sandbox de memória.

Verifique sempre:

```c
if (api->abi_version != TITAN_APP_API_VERSION ||
    api->struct_size < offsetof(TitanAppApiV1, last_field_you_use) +
                       sizeof(api->last_field_you_use))
    return -1;
```

`app_root` é o diretório instalado do aplicativo.

### `titan_app_update`

Recebe input e `delta_ms`. Não bloqueie aqui com loops, scans ou leitura enorme
de storage; quebre trabalhos próprios em etapas pequenas.

### `titan_app_draw`

Desenhe usando a API Titan. O sistema já abriu o frame e mantém a mesma área
lógica usada pela shell: **854 × 448**.

### `titan_app_shutdown`

Opcional. Libere recursos próprios aqui. O kernel também encerra qualquer stream
de áudio externo ao fechar o app.

## 7. Input

`TitanAppInput` fornece `held`, `pressed`, `released`, conexão do controle e os
quatro eixos analógicos. Os bits `TITAN_APP_BUTTON_*` estão no header público.

O aplicativo normalmente pode usar Círculo e `api->request_exit()` para sair.
Além disso, o kernel reserva **Select + Círculo** como escape de emergência; essa
combinação fecha um Titan App mesmo que ele tenha um bug na navegação normal,
desde que o `update` continue retornando.

## 8. UI compartilhada

API v1 expõe:

```text
draw_text
draw_text_shadow
draw_rect
draw_panel
draw_progress
notify
```

Esses componentes usam a fonte, regras de alpha, renderer e sistema de
notificações do Project Titan. O aplicativo não precisa recriar uma UI paralela.
Nenhum asset visual privado é copiado para o pacote.

## 9. Arquivos e dados persistentes

Leitura:

```c
api->file_read("config/default.bin", 0, buffer, sizeof(buffer));
```

O caminho é relativo à raiz instalada e rejeita caminhos absolutos, prefixos de
dispositivo, `..`, `.` e backslashes.

Escrita pública é deliberadamente limitada a:

```text
data/...
```

Exemplo:

```c
api->file_write_data("data/settings.bin", &settings, sizeof(settings));
```

A escrita usa arquivo temporário + flush + substituição atômica para reduzir
risco de corrupção após desligamento inesperado.

## 10. Fundação de mídia do kernel

A Titan App API v1 **não inclui codecs**. Ela fornece a camada de saída para que
um port de FFmpeg, libmad, libmpeg2, decoder próprio etc. não precise tomar posse
do AUDSRV ou framebuffer do sistema.

### Áudio

Formato aceito:

```text
PCM S16LE
2 canais
32000 Hz
```

Fluxo típico:

```c
api->media_begin_audio();

while (api->media_audio_free_frames() >= frames) {
    api->media_submit_pcm(samples, frames);
}

api->media_end_audio();
```

O PCM externo usa um ring buffer do kernel. SFX do sistema ainda podem ser
misturados sobre o stream.

### Vídeo

`media_present_rgb565()` recebe exatamente um frame 512×256 RGB565. O kernel
mantém uma cópia em RAM e `media_draw()` o apresenta na região escolhida pelo
app.

Em 480i o renderer usa o cached layer já existente. Em 720p não é reservada
nenhuma VRAM extra: o frame é transferido em tiles ordenados através do scratch
PSM16 já usado pelos avatares. Isso preserva o layout 720p de dois framebuffers
sem sacrificar assets residentes.

Fluxo:

```c
api->media_present_rgb565(frame, 512, 256, 512);
api->media_draw(80.0f, 70.0f, 694.0f, 347.0f, 255);
```

Para um player real, o app continua responsável por:

- abrir/receber o contêiner;
- demux;
- decodificar áudio/vídeo;
- resample para 32 kHz estéreo;
- converter vídeo para RGB565 512×256;
- sincronização A/V;
- buffering de rede/arquivo.

A API fornece **saída de mídia**, não um player pronto.

## 11. Criando o `.pkg`

Exemplo Titan-native:

```sh
python3 kernel/tools/titan_pkg.py build \
  --source ./my_app \
  --output MYAPP001.pkg \
  --title-id MYAPP001 \
  --title "Meu Titan App" \
  --version 1.0.0 \
  --type titan-native \
  --entrypoint my_app.tmod \
  --author "Seu Nome" \
  --description "Descricao curta do aplicativo"
```

Verifique antes de distribuir:

```sh
python3 kernel/tools/titan_pkg.py verify MYAPP001.pkg
```

O builder sempre força TPKG v2 para `titan-native`.

## 12. Exemplo completo

Use:

```text
docs/examples/hello_titan/
```

Com o PS2DEV configurado:

```sh
cd docs/examples/hello_titan
make
make verify
```

O resultado é `build/HELLOTITAN.pkg`.

## 13. Compatibilidade da ABI

A versão pública atual é:

```c
#define TITAN_APP_API_VERSION 1u
```

Apps devem conferir `abi_version` e `struct_size`. Novas APIs podem ser
adicionadas ao final da estrutura em futuras revisões; não assuma a existência
de campos posteriores ao tamanho que você verificou.

A ABI v1 é uma interface de desenvolvimento inicial do Project Titan Magenta
Edition e pode receber revisões antes da primeira versão pública estável. Mantenha
o código-fonte do app e declare claramente a versão de API exigida.

## SAS compatibility, compliance, and execution model

BuildFix24 separates **SAS compatibility** from **SAS compliance**. SAS compatibility is the broader acceptance property; SAS compliance is the stricter normative property. Every
compliant app is compatible, but a compatible app is not necessarily compliant.
The runtime therefore exposes `sas_compatible` and `sas_compliant` independently
and rejects the impossible state `compliant=true, compatible=false`.

Canonical classic SAS metadata is read from `title.cfg`; `app.cfg` remains a
Titan compatibility fallback and is **not** compliance proof. A sourced classic
`APP_*` layout with `title.cfg`/`boot=` and no subfolders may be compliant.
Extended/subfolder layouts and unknown three-character prefix patterns may remain
compatible while non-compliant. `INSTALL.PBT` is not required and is not SAS
evidence. See `TITAN_SAS_REFERENCE_AUDIT.md`, `TITAN_SAS_COMPLIANCE_MODEL.md`,
`TITAN_SAS_PREFIX_TABLE.md`, and `TITAN_SAS_COMPATIBILITY_MATRIX.md` before
freezing public tooling.

SAS classification does not rewrite an arbitrary ELF into a Titan-native module.
Current SAS-compatible and Unknown/Legacy ELF applications still use an external ELF handoff; classification/compliance and execution model remain separate.


## 14. BuildFix27 — Storage, Network, Image and FTP tail

BuildFix27 preserves ABI version 1 and extends `TitanAppApiV1` only at the tail. New code should
use the canonical SDK package under `sdk/titan-app/` and gate `struct_size` through the final field
it actually consumes.

New reusable boundaries include:

- `storage_list_volumes` and `storage_commit_replace`;
- asynchronous shared `text_input_open/poll`;
- asynchronous `network_get_begin/status/read/cancel`;
- `media_present_image_file` for Titan-owned JPEG/PNG FIT presentation;
- `ftp_start/ftp_stop/ftp_status`, which control a Titan-owned FTP server without exposing sockets.

The FTP backend uses bounded opaque Network Stream handles. LIST/NLST are VFS-paged, RETR is
chunked, and STOR writes to a temporary file and publishes atomically only after clean remote EOF.
Apps do not own PS2IP or FileXio.

The BuildFix27 target gate remains separate from host validation: when PS2DEV/R5900/VU tooling is
unavailable, host-passed source must remain marked TARGET_BLOCKED rather than being described as
hardware validated.
