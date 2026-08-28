<!-- Thanks for contributing to MGB64! -->

## What does this PR do?

<!-- A short description of the change and why. -->

## Checklist

- [ ] I did **not** commit any ROM or ROM-derived data (textures, audio, models,
      fonts, level/scene data, image/animation tables, logos — in `.bin` *or* as
      inline data arrays). See [DISCLAIMER.md](../DISCLAIMER.md) and
      [CONTRIBUTING.md](../CONTRIBUTING.md).
- [ ] I re-read my diff (`git diff --stat`) for accidental data blobs.
- [ ] I ran the relevant validation (`./tools/validate_quick.sh`,
      `ctest --test-dir build --output-on-failure`, or the narrower command
      appropriate for this change).
- [ ] Release/archive changes also pass
      `./scripts/ci/check_release_ready.sh` and
      `scripts/smoke_public_source_archive.sh ... --max-warnings 0` against a
      freshly generated source archive.
- [ ] Code follows the existing style (`.clang-format` / `.editorconfig`).
- [ ] Game-code behavior changes (if any) are justified; port-only fixes live in
      `src/platform/`.
