# Project Titan Magenta Edition — Network + Flow Themes RT1 BuildFix2

## Motivo

A RT1 BuildFix1 compilou, mas o `kernel.elf` resultante podia entrar em tela preta ao ser iniciado tanto pelo bootloader BF6 quanto diretamente pelo wLaunchELF. O bootloader localizava o ELF, portanto o reparo fica restrito ao startup/runtime do kernel integrado.

## Correções

### 1. Rede removida do caminho crítico do boot

`TitanNetwork_Initialize()` agora apenas inicializa estado/configuração. Nenhum `SifExecModuleBuffer()` de rede e nenhum `ps2ipInit()` é executado durante o bootstrap crítico. `TitanNetwork_Update()` aguarda 120 frames da shell antes da primeira tentativa de bootstrap de rede.

### 2. Um único driver físico de rede por vez

No boot, somente a interface selecionada é carregada:

- Wi-Fi: AR9271;
- RJ45: PS2DEV9 + SMAP.

A interface alternativa permanece lazy e só é carregada quando o usuário muda a preferência em Settings. Isso evita manter AR9271, DEV9 e SMAP simultaneamente no IOP de 2 MiB sem necessidade.

### 3. `ps2ipInit()` é o dono da inicialização NetMan no EE

Foi removida a chamada explícita redundante a `NetManInit()` antes de `ps2ipInit()`.

### 4. Runtime AR9271 compactado

O TT1426 standalone ainda trazia o trace Linux completo de laboratório. O runtime atual do caminho hardware-proven utiliza somente o bloco POSTVIF-168 (sequências históricas 2962..3129).

O IRX agora linka apenas:

- 168 descritores compactos;
- 3.632 bytes de comandos;
- 212 bytes de respostas históricas necessárias para rebinding live.

`titan_linux_coldstart_trace.o` deixa de fazer parte de `IOP_OBJS`. O fonte histórico continua preservado no projeto para auditoria, mas não entra no IRX de runtime. O `clean` também remove qualquer objeto stale dessa unidade vindo de builds anteriores.

### 5. Escopo preservado

Nenhuma alteração foi feita no bootloader oficial BF6, Installer BF6, formato TINS, UI de temas, paletas Flow ou comportamento solicitado da RT1 além do necessário para tornar o runtime de rede seguro no boot.

## Primeiro teste de hardware

Este BuildFix deve primeiro provar que o kernel volta a chegar à shell. A rede só começa a inicializar depois disso.

Sequência esperada:

```text
DEV1/R1 -> bootloader BF6 -> kernel BF2 -> shell visível
                                      -> ~120 frames
                                      -> network bootstrap
```

Ainda não reconstruir `TITAN_SYSTEM.TINS`; substituir apenas `mass:/kernel.elf` no teste.
