# Third-party notices

O segundo estagio e seu layout de memoria seguem a estrategia do `elf-loader`
do PS2SDK. O PS2SDK pertence aos respectivos autores do projeto PS2DEV e e
distribuido sob a Academic Free License 2.0.

Uma copia da licenca esta em `licenses/PS2SDK-AFL-2.0.txt`.

Fontes oficiais de referencia:

- https://github.com/ps2dev/ps2sdk
- `ee/elf-loader/src/elf.c`
- `ee/elf-loader/src/loader/src/loader.c`
- `ee/elf-loader/src/loader/linkfile`

A imagem de boot foi fornecida pelo usuario para uso no Project Titan.

## PS2RD resident cheat engine

The optional resident code handler used by Titan's game-patch handoff is derived
from the PS2RD low-level cheat engine by Mathias Lafeldt (misfire). That source
remains licensed under GNU GPL version 3 or later and is not relicensed by the
Project Titan MIT license.

Relevant files:

- `stage2/src/titan_ps2rd_engine.S`
- `stage2/PS2RD-GPL-3.0.txt`

The production Neutrino and Titan UL binaries are not rebuilt by this feature.
