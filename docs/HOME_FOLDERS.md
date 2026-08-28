# Home folders — OrbisOS-compatible interaction model

The Project Titan Magenta Edition Home folder flow intentionally mirrors the
PlayStation 4 OrbisOS interaction shown in the reference capture supplied for
this development pass.

## Controls

- `SELECT` on PlayStation 2 replaces the PS4 `OPTIONS` button.
- `X` confirms, opens a folder, toggles content selection or launches the
  selected game inside an open folder.
- `O` cancels/goes back.
- D-pad navigates the contextual menu, editor, content grid and open folder.

## Flow

1. Highlight a game on Home and press `SELECT`.
2. Choose `Adicionar a pasta`.
3. Choose `Adicionar a uma nova pasta`, or an existing folder.
4. The editor presents Name, Number of items, Content, preview, Cancel and OK
   using the OrbisOS geometry with the Magenta Edition black/graphite skin.
5. `Conteudo > Selecionar` opens the checkbox grid with Select all,
   Deselect all and Confirm actions.
6. Confirming the editor persists the folder and returns focus to its tile.
7. Opening a folder expands its content row below the folder tile.
8. `SELECT` on a folder exposes Edit folder, Delete folder and Information.

## Visual transition contract

The Orbis screen flow is preserved, but the blue full-screen wash is intentionally
replaced by a near-black translucent backdrop with Magenta focus accents. Moving
between Add to folder, New/Edit folder and Select content now cross-fades the
outgoing and incoming screens instead of switching them in a single frame. The
context panel keeps its right-side slide animation and participates in the same
cross-fade when entering or leaving the full-screen folder flow.

## Persistence

Folder membership is stored as a compact, stable 64-bit case-insensitive key
derived from each game's canonical path, never by volatile catalog index. This
keeps the persistent identity stable while avoiding the RAM cost of retaining a
full path buffer for every folder slot. The layout file is `TITAN/HOMEFOLD.DAT`;
saves publish through a temporary file and keep a last-known-good backup
(`HOMEFOLD.BAK`). Corrupt or interrupted primary writes recover from the backup
when possible.

Deleting a folder deletes only the grouping metadata. Games return to the main
Home grid and no game data is removed.

The on-disk format currently supports 16 folders, each capable of referencing
the full Titan game catalog capacity.

## Engine terminology

ISO entries are exposed as **Titan-Neutrino Engine** content. `ul.cfg`/
USBExtreme entries are exposed as **Titan UL Engine** content. The Home context
menu never labels these as generic Neutrino options.
