# BuildFix30 — FMCB autonomous compatibility matrix

| Console / ROM state | Region/model evidence | FMCB autonomous source state | Target build | Hardware | Result |
|---|---|---|---|---|---|
| Known retail Boot ROM < 2.20 | coherent known family/region | SOURCE_COMPLETE candidate path | PENDING_EXTERNAL | PENDING | Candidate supported; physical validation required |
| Known retail Boot ROM = 2.20 | coherent known family/region | SOURCE_COMPLETE candidate path | PENDING_EXTERNAL | PENDING | Candidate supported; physical validation required |
| Boot ROM > 2.20 | any | gated fail-closed | N/A | negative test pending | UNSUPPORTED for Magenta autonomous FMCB |
| Boot ROM unknown/unreadable | any | gated fail-closed | N/A | pending | UNKNOWN; autonomous installation refused |
| Identity/region inconsistent | inconsistent/unknown | gated fail-closed | N/A | pending | UNKNOWN/UNSUPPORTED; autonomous installation refused |
| Existing unmanaged/unknown/corrupt FMCB | any otherwise-compatible console | protected fail-closed | N/A | pending | No blind overwrite; recovery/migration must be explicit |
| Titan-managed FMCB | otherwise-compatible | detectable managed state | PENDING_EXTERNAL | PENDING | maintenance/recovery state; normal PUP does not reinstall FMCB |
| Semi-autonomous Matrix/Modbo DEV1/R1 | external shortcut | preserved baseline | existing target validation separate | historically known-working baseline | FMCB not required |

`SOURCE_COMPLETE` here means the source path exists and passes host/source contracts; it is not a claim of target build or physical hardware validation.
