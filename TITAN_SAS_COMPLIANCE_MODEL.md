# Titan SAS Compliance Model — BuildFix24

## Invariant
`SAS_COMPLIANT => SAS_COMPATIBLE`.
The inverse is false. Titan rejects/normalizes any state that attempts compliant=true while compatible=false.

## Properties
- `sas_compatible`: Titan can safely recognize/launch the content through the SAS compatibility path.
- `sas_compliant`: the content meets the currently audited normative classic SAS rules.
- `sas_profile`: CLASSIC, EXTENDED_COMPAT, TITAN_FALLBACK, or NONE.
- `sas_has_subfolders`: structural fact, independent of compatibility.
- `sas_osdsys_manageable`: true only when classic layout is rooted directly where OSDSYS Memory Card management can preserve it.
- `sas_copyability`: OSDSYS_COPY_SAFE, OSDSYS_COPY_UNSAFE, or UNKNOWN.
- `sas_prefix_known`: exact prefix exists in the centralized sourced table.

## Classic compliant application profile
Current BuildFix24 compliance requires all of:
1. SAS compatibility recognized safely;
2. sourced normative `APP` prefix;
3. canonical `title.cfg` metadata;
4. `boot=` naming the discovered ELF;
5. flat application directory (no subfolders).

A compliant app is therefore always compatible.

## Compatible but non-compliant examples
- extended/subfolder layout;
- unknown 3-letter OSDMenu-compatible prefix;
- Titan `app.cfg` fallback without canonical SAS metadata;
- canonical metadata missing required structural evidence.

## Metadata evidence classes
- CANONICAL SAS METADATA: `title.cfg` when structurally valid for classic SAS.
- COMPATIBILITY METADATA: OSDMenu-compatible prefix pattern and compatible package structure.
- TITAN FALLBACK: `app.cfg`.
- TITAN HEURISTIC: filename/name fallback; does not establish SAS compatibility by itself.

## INSTALL.PBT / PBAT
`INSTALL.PBT` is not required for discovery, compatibility, compliance, installation, launch, badge or classification. PBT scripts are treated as companion BootManager/Crystal Chip mechanisms unless a future audited rule says otherwise.

## Archive formats
PSU/TAR/PAX are delivery/container choices. Archive format does not establish content compliance. Import validation is handled separately by the host/SDK importer contract.
