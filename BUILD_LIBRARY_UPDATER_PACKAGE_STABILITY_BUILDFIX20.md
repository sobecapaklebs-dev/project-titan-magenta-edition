# BuildFix20 — Orbis Library fidelity, TPUP updater and Package Installer stability

## User-visible changes

- Library has its own Orbis-style shelf glyph and is normalized to the final
  Home tile position on every load/layout mutation.
- Library > Applications keeps the previous Applications glyph.
- Folder `Selecionar conteudo` uses a 6-column x 3-row Orbis-style scrolling
  viewport, attached selection boxes, side actions, footer-safe geometry and a
  scrollbar over the complete candidate catalog.
- Settings > System now includes `Atualizacao do software do sistema`, using
  the same glyph as Debug Settings and the standard hierarchical menu motion.

## Package Installer

- discovery capacity: 96 entries;
- both `mass:/TITAN/packages` and `mass0:/TITAN/packages` are scanned on every
  refresh;
- `.pkg` and `.tthm` remain supported;
- exclusive storage ownership is released after each completed transaction;
- a second package can start without leaving Package Installer;
- installed apps are appended to the live application catalog and trigger an
  app-icon atlas rebuild rather than a full Neutrino/UL library scan.

## System updater

TPUP v1 `.pup` support includes:

- USB discovery from `TITAN/update` and `TITAN/packages`;
- monotonically increasing build numbers (no downgrade/equal-build update);
- safe target allow-list;
- table + payload CRC32 verification;
- staged extraction;
- rollback-safe multi-file publication;
- transaction backups removed on success.

BuildFix20 is the bootstrap release for the updater. BuildFix21 is intended to
be the first update delivered and tested primarily as `.pup`.

## Host validation

Focused contracts cover Library/folder behavior, Package Installer sequential
transactions, TPUP builder/verifier and a full staged `kernel.elf` update
transaction. PS2DEV linking and physical-PS2 validation remain authoritative.
