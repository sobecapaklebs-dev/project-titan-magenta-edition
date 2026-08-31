# Project Titan 0.3.90-rc3 — correção do modo 720p

## Diagnóstico

A emenda horizontal mostrada na evidência não era um erro de escala nem de
temporização do CRTC. O renderer RC2 alocava apenas um framebuffer nativo
1280×720 PSM16. O GS exibia e renderizava na mesma superfície; esperar o VSync
no início do frame apenas deslocava a linha em que a varredura alcançava a nova
renderização. A fotografia contém, portanto, parte do frame anterior e parte do
frame seguinte separadas por uma linha horizontal.

Havia também um segundo defeito na troca de modo: o mapeamento lógico era
calculado consultando o modo ainda ativo antes de `TitanVideoMode_Commit()`.
Ao entrar em 720p, a primeira configuração recebia regras do modo legado.

## Correção aplicada

- 720p agora usa dois framebuffers PSM16 e só apresenta o back buffer concluído
  depois de `graph_wait_vsync()`.
- Cada framebuffer continua visível como 1280×720, mas reserva 1280×768 na VRAM.
  As 48 linhas físicas ocultas mantêm as páginas GS consecutivas e sem
  sobreposição.
- A troca 480i ↔ 720p reconstrói toda a VRAM, reinstala os descritores, limpa os
  dois buffers, reenvia recursos residentes e só então confirma o CRTC. Falhas
  restauram integralmente o layout anterior.
- O mapeamento lógico passa a receber explicitamente o modo solicitado, sem
  consultar estado antigo.
- Para comportar dois buffers nativos nos 4 MiB do GS sem alterar os assets:
  - máscaras alfa de fonte e UI são guardadas como índices PSM8 exatos;
  - uma CLUT RGBA32 compartilhada recompõe exatamente cinza e alfa;
  - avatares mantêm seus pixels PSM16 originais e são enviados em blocos de
    oito células para uma textura scratch;
  - o cache opcional do teclado usa o fallback de desenho ao vivo que já
    existia no projeto.

## Orçamento exato de VRAM

| Modo | Recursos | Framebuffers | Total | Livre |
|---|---:|---:|---:|---:|
| 480i | 1.851.392 B | 2 × 1.146.880 B | 4.145.152 B | 49.152 B |
| 720p | 262.144 B | 2 × 1.966.080 B | 4.194.304 B | 0 B |

## Versionamento

- Kernel: `0.3.90-rc3`
- UI: `Titan UI 0.9.23`
- Bootloader metadata: `0.1.4-rc3`, apontando para o Kernel/UI acima

## Validação executada

- `scripts/validate_source.sh`: aprovado integralmente.
- Backend GS compilado sintaticamente no host com `-Wall -Wextra -Werror`.
- Contrato novo de 720p: double buffer, troca em VSync, altura física 768,
  mapeamento pelo modo solicitado e orçamento exato de 4 MiB — aprovado.
- Hashes dos 236 arquivos sob `kernel/**/assets/**`: idênticos à baseline RC2.
- Integridade do ZIP de entrada: aprovada; SHA-256 da baseline:
  `91b91ad9ad72197151f22cbfb44c6f60771d59551e56337f17bcc96c8cfbf3c0`.

## Limite da validação

Este ambiente não possui `PS2DEV`, `PS2SDK` nem o compilador EE. Por isso não
foi produzido nem alegado um build MIPS, teste em emulador ou teste no PS2
físico. A confirmação final da remoção da emenda deve ser feita no hardware.

## Comandos de build e instalação

```sh
cd "Project Titan Magenta Edition"
scripts/validate_source.sh
JOBS=8 scripts/build_all.sh --verify-reproducible
scripts/install_to_usb.sh --target "/run/media/$USER/PS"
```
