# Project Nes — teste de hardware

## Primeiro teste: nenhum arquivo extra

O `PROJECTNES.pkg` contém uma ROM homebrew própria chamada `ProjectNesTest.nes`.
Depois da instalação, abra **Project Nes**, mantenha `ProjectNesTest.nes` selecionada e pressione **X**.

O resultado esperado é:

- a superfície do emulador abre dentro do Project Titan;
- aparece `PROJECT NES / READY NOW`;
- a cor do texto muda periodicamente via NMI;
- um tom simples é produzido pela APU de preview;
- **Triângulo** pausa/continua;
- **Quadrado** executa reset;
- **Select + Círculo** encerra o app e retorna diretamente à HOME do Titan.

Nenhuma ROM comercial é incluída no pacote.

## Teste com uma ROM real NROM

A API Titan v1 ainda não possui enumeração de diretórios para apps. Nesta preview existe um slot de teste
fixo. Copie uma ROM iNES Mapper 0/NROM para:

```text
mass:/TITAN/apps/PROJECTNES/data/roms/game.nes
```

Feche e reabra Project Nes. `game.nes` passa a aparecer como segunda opção da biblioteca.

Compatibilidade 0.1: NROM-128/NROM-256, CHR ROM ou CHR RAM, mirroring horizontal/vertical. Jogos que
necessitam de mapper diferente ou instruções ilegais do 6502 são recusados de forma controlada.

## O que observar no PS2

1. instalação do PKG e criação da tile;
2. entrada e saída do app sem `ExecPS2`;
3. estabilidade da shell ao usar **Select + Círculo**;
4. imagem em 480i e no modo 720p do Titan;
5. áudio sem impedir o retorno da música do sistema após sair ou após erro de ROM;
6. resposta do controle;
7. comportamento de uma ROM NROM real.

Se ocorrer falha, anote se ela acontece em: abertura do app, leitura da ROM, primeiro frame, áudio ou saída.
