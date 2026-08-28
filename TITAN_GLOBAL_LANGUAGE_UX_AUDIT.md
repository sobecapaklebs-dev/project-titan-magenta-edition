# Titan Global Language / Text / UX Audit — BuildFix30 CP05

## Policy
English is the canonical Magenta Edition v1 shell language. A single Titan Localization Service now owns user-facing language selection and provides English, Portuguese (Brazil) and Spanish tables for lifecycle-critical/global strings. Portuguese/Spanish are prepared as supported localization targets without claiming that every historical developer/DEX sentence has been translated.

## Material corrections
- Removed the pre-BF30 mixture of Portuguese and English literals from the primary HOME, Network, System Update, folder/application lifecycle and power/error paths by normalizing legacy literals to canonical English.
- Health & Safety, Welcome, Settings category names, HOME launch-status strings and Theme common state strings are no longer hard-coded to one language.
- Added Settings > System > Language with English / Português (Brasil) / Español selection.
- Language preference is persisted transactionally to `mass:/TITAN/system/language.cfg` using the shared storage worker; changing language never performs filesystem I/O from the frame/input path.
- UTF-8 decoding of all kernel source `.c/.h` files is a host contract, protecting accented Portuguese/Spanish strings from invalid source encoding.

## Layout/UX rule
The localization API returns immutable strings and does not allocate in draw paths. Existing text measurement/clipping remains the layout authority. New global strings are intentionally concise; longer Portuguese/Spanish variants use the same measured-centering paths on Health/Welcome.

## Scope truth
This checkpoint closes the language architecture and canonical-text inconsistency that existed in BF29/BF30 CP04. DEX-only technical telemetry may remain canonical English where no translation exists; this is deliberate fallback behavior, not mixed-language corruption. Installer language selection remains its existing independent pre-install flow and is not rewritten during this release pass.

Target/PCSX2/physical visual truncation validation remains external.
