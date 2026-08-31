# BuildFix19 — Orbis Library + global hierarchical menu motion

## Scope

BuildFix19 turns the old Home `Applications` system tile into the PS4-style `Library` hub and makes the Library the complete catalog behind Home's fixed 15-tile presentation limit.

Reference behavior/layout: user-supplied OrbisOS Library screenshot and video. The reference theme colors are intentionally ignored; Project Titan's active theme continues to own colors/background.

## Library

Home shell tile:
- title: `Library`
- Title ID remains `TITN00001`
- the existing Applications/Library tile icon ID is preserved (`TITAN_ICON_APPLICATIONS`)
- page: `TITAN_SHELL_PAGE_LIBRARY`

The previous general `Applications` Home system tile is gone. `★ APP_HOME` remains the DEX-only complete application launcher and still routes to the legacy Applications page.

Library layout mirrors OrbisOS:
- `Library` title + top separator
- Search row
- `This PS2` section
- `All`
- `Folders`
- `Games`
- `Applications`
- no `Purchased`/store category
- category counters
- `Name: A-Z` sort control
- three-column cover/text grid
- right-side scrollbar showing catalog position

Library data is built from the complete catalogs, not the Home projection:
- `TitanHomeFolders_GetCount()`
- `TitanNeutrino_GetGameCount()`
- `TitanApplications_GetCount()`

All entries are sorted A-Z. Search opens the existing Titan text input and filters the complete combined catalog case-insensitively.

Controls:
- sidebar: UP/DOWN
- Search + X: open search input
- category + X/RIGHT: enter content grid
- grid: D-PAD
- LEFT at first column: return to categories
- X: open folder / launch game / launch application
- O: grid -> categories -> Home

## Catalog capacity

The game catalog capacity grows from 128 to 256 records so the Library/folder selector can expose substantially more USB/HDD content. Folder membership remains capped at 128 items, decoupled from catalog capacity, avoiding a large stack/storage regression in the folder service.

## Folder content selector

The folder content screen no longer pages in hard 24-item jumps. It keeps a four-row viewport but scrolls one row at a time as the cursor moves through the complete candidate list. A vertical scrollbar shows beginning/middle/end position.

Editor -> Select Content, Select Content -> Editor and other folder child screens now use parent-left / child-from-right motion with fade/easing rather than an alpha-only cut.

## Global hierarchical menu motion

Three menu hierarchies now share the same Orbis-style rule:
- Settings root -> every Settings category (including Themes and ★Debug Settings)
- ★Debug Settings -> Package Installer
- Folder editor/destination/content child screens

The transition rule is:
- parent eases left and fades
- child enters from the right and fades in
- back reverses the motion

## Compatibility

`TITAN_SHELL_PAGE_LIBRARY` is appended to `TitanShellPage` rather than inserted in the middle. This deliberately preserves numeric values previously persisted in folder shell-item keys.

## Validation

Passed:
- full `scripts/validate_source.sh`
- `orbis-library-global-motion-contract: OK`
- `project-titan-source-validation: OK`
- curated asset validation
- host syntax check for `titan_shell_scene.c` with `-Wall -Wextra -Werror`

No MIPS/R5900 final link was performed in the build environment.
