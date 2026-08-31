# BuildFix30 — FMCB build wiring

The source integration is rooted at `third_party/fmcb/`.

`third_party/fmcb/Makefile` uses the normal PS2 environment (`$PS2DEV`, `$PS2SDK`) and builds the imported FMCB installer. `tools/prepare_titan_fmcb_bundle.py` validates the imported source, prepares the Titan FMCB bundle, installs the Titan-specific `FREEMCB.CNF`, computes the required-space budget and emits an identity manifest.

Host source check:

```sh
python3 tools/prepare_titan_fmcb_bundle.py --project . --source-check
```

Target build requires a PS2DEV/PS2SDK environment. This executor does not contain the R5900 PS2 toolchain, so target compilation is intentionally reported separately from source/host validation.
