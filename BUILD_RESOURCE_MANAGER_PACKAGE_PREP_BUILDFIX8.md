# RC14 BuildFix8 — Resource-managed Package Preparation

## Why BuildFix7 could remain at 0%

BuildFix6/7 moved package extraction off the UI thread and made payload transfers
cooperative, but the complete pre-extraction path still ran as one monolithic
worker call. Before the first payload byte it could perform package stat/open,
full header/table reads, destination checks, recursive stale-stage deletion and
stage recreation. A slow USB/FAT operation therefore left the UI alive while
status remained at `Preparando pacote` / 0%.

## Resource-management model

BuildFix8 applies a small RTOS-style workqueue contract to Package Installer:

- the UI only submits/cancels work;
- the storage worker owns filesystem operations;
- package installation retains exclusive ownership of the storage lane;
- package preparation is a state machine with one bounded I/O operation per
  worker dispatch;
- every preparation operation publishes its exact state to the UI before the
  filesystem call;
- payload extraction remains one read OR one write per worker quantum;
- scanner/assets/VMC stay parked for the complete Package Installer session;
- menu music remains deliberately suspended during installation.

Normal non-package scheduling keeps the existing class-like order: urgent audio,
foreground storage work, VMC refresh, deferred audio, asset building and finally
library scan.

## Transaction staging

The old fixed stage directory (`.install-<TITLEID>`) required recursive cleanup
before a retry could begin. BuildFix8 replaces it with generation staging:

    .install-<TITLEID>-00000001
    .install-<TITLEID>-00000002
    ...

If a previous generation exists, the installer advances to the next generation.
It never recursively removes stale staging from the interactive installation
path. Failed/cancelled generations remain private and are not published as apps.
A successful transaction is still committed by renaming its private stage to the
final application directory.

This deliberately trades a small amount of recoverable disk space after an
abnormal interruption for deterministic interactive latency and avoids making a
future installation depend on recursive USB cleanup.

## Preparation states visible on hardware

The status message now identifies the operation immediately before it executes:

- `Preparando: consultando pacote`
- `Preparando: abrindo pacote`
- `Preparando: lendo cabecalho`
- `Preparando: validando cabecalho`
- `Preparando: lendo tabela de arquivos`
- `Preparando: validando tabela`
- `Preparando: reservando destino`
- `Preparando: verificando TITAN`
- `Preparando: verificando apps`
- `Preparando: verificando instalacao existente`
- `Preparando: criando transacao`

If physical hardware still blocks inside a single filesystem/RPC call, the last
visible state identifies the exact boundary without disk logging.

## Validation

`bash scripts/validate_source.sh` passes in full after this change.

Package Installer host coverage additionally:

- limits reads/writes to 3 bytes per transfer;
- verifies the new PREPARING state;
- pre-creates a stale generation to simulate a previously interrupted install;
- verifies the stale generation is not recursively deleted on the critical path;
- verifies the next generation installs and publishes normally;
- preserves checksum, cancellation and transaction tests.

No physical PS2/MIPS build is claimed by this note.
