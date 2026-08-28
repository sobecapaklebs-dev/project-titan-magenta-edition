# Hello Titan

Exemplo mínimo de um **Titan App** nativo. O arquivo `app.c` é compilado como
um objeto MIPS `ET_REL` (`.tmod`), sem etapa de link independente. O kernel do
Project Titan carrega e reloca o módulo dentro do próprio processo e entrega a
`TitanAppApiV1` em `titan_app_init`.

Com `PS2DEV` no `PATH`, a partir desta pasta:

```sh
make
make verify
```

O pacote final será `build/HELLOTITAN.pkg`. Copie-o para
`mass:/TITAN/packages/`, ative DEX e abra **★ Debug Settings > Package
Installer**.

O script `make_example_assets.py` só gera o ícone de demonstração; ele não
altera nenhum asset do Project Titan.
