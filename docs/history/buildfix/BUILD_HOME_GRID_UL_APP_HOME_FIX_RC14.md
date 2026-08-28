# RC14 Home grid, UL catalog and APP_HOME correction

RC14 is based directly on the verified RC13 full-source package.

## Corrected behavior

- The collapsed Home row remains fully above the footer while the system menu
  is open.
- Footer control hints are measured and scaled to the logical viewport.
- The incremental scanner retains every valid 64-byte USBExtreme record and
  deduplicates UL games by startup ID, not by the common `ul.cfg` path.
- The scanner checks `mass0:` even when `mass:` already contains ISO games.
- UL folder membership receives a stable startup-based key; old RC13 path keys
  remain readable and apply to one deterministic UL record only.
- Unresolved folder keys no longer draw a false game placeholder.
- APP_HOME now draws the explicitly supplied four-squares-and-database icon as
  a centered transparent GS alpha mask.

## Preserved behavior

RC13 Titan UL build ordering, RC11 navigation audio and background music,
native 720p double buffering, storage fallback, profiles, VMC, DEX, game
handoff, Package Installer and all other curated UI assets are unchanged.

## Validation boundary

The host suite verifies multiple UL records, primary-plus-fallback scanning,
unique UL folder keys, grid/footer geometry, APP_HOME transparency and asset
integrity. This environment does not contain PS2DEV/PS2SDK and cannot replace
PCSX2 or physical-PS2 validation.
