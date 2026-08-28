---
name: Platform validation report
about: Report a clean build/test/run result for a supported platform
title: "Validation: platform "
labels: validation, build
---

**Do not attach ROMs, extracted assets, save files, screenshots, video, or
audio captures. Text logs are fine.**

### Platform
- OS / version:
- MSYS2 environment, if Windows (`echo $MSYSTEM`):
- GPU / driver, if an interactive run was tested:
- Commit SHA:

### Tool versions
Paste the relevant text output.

```sh
cmake --version
cc --version
python3 --version
pkg-config --modversion sdl2 || pkgconf --modversion sdl2
```

### Commands tested
Mark each as pass/fail and paste concise text logs for failures.

- [ ] `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- [ ] `cmake --build build --parallel`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `./tools/validate_quick.sh`
- [ ] `./build/ge007 --rom /path/to/baserom.u.z64`

### Notes
Mention missing dependencies, documentation gaps, runtime issues, or anything
that should become a separate bug report.
