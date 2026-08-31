# Titan App Platform SDK v1 — Build Fix 1

This revision fixes the clean/parallel kernel build dependency introduced by the Titan App dynamic icon atlas.

## Fixed

- `src/services/titan_app_assets.o` now explicitly depends on `$(TITAN_UI_AVATAR_HEADER)`.
  After `make clean-generated`, GNU Make therefore regenerates `src/graphics/generated/titan_ui_avatars_data.h` before compiling `titan_app_assets.c`.
- Removed the duplicate `fallback-system-assets` recipe from `kernel/Makefile`.
- Replaced two warning-prone bounded `strncpy` uses in VMC status caching and library scanning with explicit NUL-terminated bounded copies.
- Added source-hardening regressions for the generated avatar-header dependency and duplicate fallback target.

## Assets

No file under `kernel/assets/ui/` was changed.
