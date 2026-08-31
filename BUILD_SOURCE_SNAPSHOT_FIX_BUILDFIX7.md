# Project Titan Magenta Edition 0.3.90 RC14 — Source Snapshot BuildFix7

## Hardware-build failure fixed

`bash scripts/build_all.sh` could fail in `kernel/tests/test_release_contract.py` with:

```
FileNotFoundError: .../bootloader/include/titan_boot_version.h
```

The failure was caused by `scripts/build_all.sh` switching to `git archive HEAD`
whenever the extracted Project Titan tree happened to live at the root of an
existing Git checkout. `git archive HEAD` only contains files committed in HEAD,
so hotfix files and modified source from the release tree could disappear from
the disposable `/tmp/titan-build.*` snapshot.

BuildFix7 removes that split behavior. Release builds now always snapshot the
current source tree with `tar`, while excluding `.git`, build outputs and generated
production headers. Therefore the files visible in the source directory are the
files actually compiled.

This also prevents a broader class of false builds where a developer tested one
working tree but `build_all.sh` silently compiled an older committed HEAD.

The same protection was also applied to `scripts/package_source.sh`, preventing a
future source package made from a dirty Git checkout from silently reverting to
committed `HEAD`.
