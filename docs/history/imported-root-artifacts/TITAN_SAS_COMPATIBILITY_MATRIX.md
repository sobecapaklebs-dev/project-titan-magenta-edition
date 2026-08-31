# Titan SAS Compatibility Matrix — BuildFix24

| Case | Compatible | Compliant | Profile | OSDSYS manageable | Copyability | Reason |
|---|---:|---:|---|---:|---|---|
| `mc0:/APP_NAME/NAME.ELF` + canonical `title.cfg`/`boot=` + flat | yes | yes | CLASSIC | yes | OSDSYS_COPY_SAFE | sourced APP prefix + canonical metadata + flat MC-root structure |
| `APP_NAME` + title.cfg + subfolder(s) | yes | no | EXTENDED_COMPAT | no | OSDSYS_COPY_UNSAFE | extended hierarchy is not classic compliant |
| unknown 3-letter `XYZ_NAME` + valid title.cfg | yes | no | EXTENDED_COMPAT | no | depends on hierarchy | compatibility pattern only; prefix not normative |
| `APP_NAME` + `app.cfg`, no canonical title.cfg | yes | no | TITAN_FALLBACK | no | depends on hierarchy | Titan compatibility fallback |
| generic ELF + `INSTALL.PBT` only | no | no | NONE | no | UNKNOWN | INSTALL.PBT is not SAS evidence |
| generic ELF/name heuristic only | no | no | NONE | no | UNKNOWN | Titan legacy fallback |
| unknown/incomplete safely parsed package | may be unknown | no | NONE/compat | no | UNKNOWN | UNKNOWN is not automatically invalid |

Archive format (PSU/TAR/PAX) does not alter this matrix by itself.
