# Titan SAS Reference Audit — BuildFix24 CP01

Status: REFERENCE AUDIT COMPLETE / NO PRODUCT SEMANTICS CHANGED IN CP01

## Scope
This audit compares the BuildFix23 Titan SAS assumptions against primary/project-author sources before public SDK freeze. It deliberately separates normative claims from interoperability observations.

## Primary / project-author references

### R1 — TnA, “SAS Standard - Information&Structure”, PSX-Place
URL: https://www.psx-place.com/threads/sas-standard-information-structure.44785/
Author: TnA (thread starter; PS2 Application System project)
First post: 2024-09-18; later clarifications through 2025-03-29.
Confidence: HIGH for the intended SAS structure.

Material findings used by Titan:
- SAS folder names use a prefix plus app name separated by `_`; `APP_OPL` is the explicit example.
- `title.cfg` is described as mandatory for APP folders.
- executable naming defaults to the app-name portion or a static `BOOT.ELF`-style name; custom selection may exist through companion/theme mechanisms.
- TnA clarifies Memory Card classic path as `mc:/PREFIX_APPNAME/APPNAME.ELF`, HDD under common/APPS, and other devices under `APPS/PREFIX_APPNAME/...`.
- prefixes are “mostly APP” but can vary depending on category; the accessible primary text does not publish a complete frozen prefix table.
- TnA explicitly states that sub-folders on Memory Card are not SAS-compliant.
- APPINFO.PBT discussion is Crystal Chip/BootManager integration context, not evidence that INSTALL.PBT or any PBT is a normative SAS requirement.

### R2 — OSDMenu/HOSDMenu project description
URL: https://www.psx-place.com/resources/osdmenu.1588/
Maintainer/project source: OSDMenu/HOSDMenu.
Confidence: HIGH for launcher interoperability; MEDIUM for SAS normativity.

Material findings:
- OSDMenu launches SAS-compatible applications from Memory Card browser when directory is `BOOT`, `<3-letter SAS prefix>_<appname>`, or ELF fallback.
- HOSDMenu applies similar SAS-compatible recognition on HDD/common and Memory Card.
- This is evidence for compatibility-oriented recognition and for a three-letter prefix convention, not a complete normative prefix table.

### R3 — PS2Wiki SAS Apps Archive
URL: https://ps2wiki.github.io/sas-apps-archive/
Project archive maintained for PS2 Application System packages.
Confidence: MEDIUM-HIGH for ecosystem examples; not treated as sole normative authority.

Material findings:
- archive distinguishes APP PSUs, System-Apps, Games, RTEs and other packages;
- archive calls its entries SAS-compliant and distributes them as PSU packages;
- category structure demonstrates that “SAS” is the standard while package/application categories are not automatically independent standards.

### R4 — Open PS2 Loader title.cfg implementation/documentation
URL: https://github.com/ps2homebrew/Open-PS2-Loader
Confidence: HIGH for `title.cfg` interoperability, NOT a normative SAS source by itself.

Material findings:
- OPL’s folder-based APPS method uses a `title.cfg` containing at least title/boot fields.
- This supports the SAS reference to OPL-like mandatory `title.cfg`, but Titan must not infer that every OPL `title.cfg` package is SAS-compliant.

## Explicit user/TnA clarification accepted as BuildFix24 normative input
The BuildFix24 task additionally supplies the following normative corrections, which are consistent with the reference audit and must be represented directly in Titan:
- `SAS_COMPLIANT => SAS_COMPATIBLE`.
- `SAS_COMPATIBLE` does not imply `SAS_COMPLIANT`.
- `INSTALL.PBT` is not a SAS requirement; presence does not prove SAS and absence does not invalidate SAS.
- classic flat layout intentionally preserves OSDSYS manageability.
- extended/subfolder layouts may be SAS-compatible while non-compliant.
- archive/delivery format (PSU/TAR/PAX) is distinct from content-standard classification.

## BuildFix23 assumptions requiring correction

1. **Single SAS classification bit**
   BuildFix23 has `TITAN_APPLICATION_CLASS_SAS` but no separate compatible/compliant properties.
   Status: SEMANTICALLY INSUFFICIENT.

2. **Metadata priority presented as SAS rule**
   BuildFix23 docs state `app.cfg -> title.cfg -> folder prefix -> name heuristic` as SAS/mSAS resolution.
   Status: INCORRECT AS NORMATIVE SAS DOCUMENTATION.
   Titan may retain `app.cfg` as a Titan compatibility fallback, but it must not establish compliance.

3. **Folder prefix heuristic promotes directly to SAS**
   BuildFix23 accepts any alphanumeric prefix before `_` and marks the app SAS.
   Status: TOO BROAD; creates false positives.

4. **No classic/extended layout distinction**
   BuildFix23 does not model subfolders, OSDSYS manageability or copyability independently.
   Status: MISSING.

5. **No evidence tiering**
   Canonical standard metadata, compatibility metadata, Titan fallback and Titan heuristic are not distinguished.
   Status: MISSING.

6. **INSTALL.PBT**
   No BuildFix23 requirement was found in TitanApplications, which is correct. BuildFix24 will add a negative contract so this cannot regress.

## Frozen CP01 interpretation for CP02–CP04

- Do not replace the broad `Titan SAS` presentation identity yet; add truth-bearing properties beneath it.
- Compliance is a validator result, not a consequence of branding.
- Runtime discovery remains tolerant and safe.
- `title.cfg` is canonical evidence for classic SAS APP folders, subject to structural/prefix requirements.
- `app.cfg` is Titan compatibility metadata/fallback only unless a future primary source establishes otherwise.
- Prefix recognition must come from one centralized table. The table may contain only entries supported by a cited source; unknown three-letter prefixes may be compatibility hints but cannot automatically confer compliance.
- Memory Card subfolders make the layout non-compliant under the cited TnA clarification, while compatibility may remain true.
- PBT files are companion/BootManager mechanisms unless a specific SAS requirement is established; `INSTALL.PBT` is never required by Titan SAS validation.
- PSU/TAR/PAX describe delivery/import containers and do not themselves prove compliance.

## Open evidence item
The accessible primary TnA text says prefixes are mostly `APP` and may vary, but does not expose a complete canonical list. Therefore CP03 must **not invent additional normative prefixes**. The first frozen table will include `APP` as high-confidence normative evidence and may encode compatibility-only prefix-pattern handling separately. Additional category prefixes require a direct project source before becoming normative.
