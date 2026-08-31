# Titan SAS Prefix Table — BuildFix24

This table is centralized in `kernel/src/services/titan_sas.c`. A prefix is not added as normative merely because an application happens to use it.

| Prefix | Category | Source | Confidence | Titan interpretation | Notes |
|---|---|---|---|---|---|
| `APP` | Application | TnA, “SAS Standard - Information&Structure” | HIGH | Normative SAS application prefix | Explicit `APP_OPL` example; TnA says most are `APP`, but prefixes may vary. |

## Compatibility-only pattern
OSDMenu documents launching `<3-letter SAS prefix>_<appname>`. Titan therefore accepts an unknown **three alphanumeric character** prefix as a compatibility hint. It remains `sas_prefix_known=0` and can never establish SAS compliance until that exact prefix is added to this sourced table.

No additional normative prefixes are frozen in BuildFix24 because the audited primary text does not publish a complete list.
