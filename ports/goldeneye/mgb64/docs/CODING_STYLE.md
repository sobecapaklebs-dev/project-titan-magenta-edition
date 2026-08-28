# Coding Style

This project has two different kinds of C code:

- decompiled game and matching-target code, where preserving original structure
  and known symbol names matters most;
- native-port support code, where readability, narrow platform seams, and clear
  review boundaries matter most.

When those goals conflict, matching/parity wins in decompiled game code and
maintainability wins in platform-owned code.

## Formatting

- Use the repository's `.clang-format` and `.editorconfig` settings for files
  you touch.
- Keep local style consistent with surrounding code. Decompiled files often
  reflect the original game's structure; avoid broad restyling in those files.
- Do not reformat generated or third-party code such as `lib/glad/` unless the
  change is specifically about regenerating or updating that dependency.

## Naming

- Preserve known canonical names for matched or decompiled symbols, even when
  they do not match modern style preferences.
- For new project-owned C symbols, prefer a short subsystem prefix plus a clear
  verb or noun, for example `chrResetNearMiss`, `portAudioTraceWrite`, or
  `romIoRead`.
- Use `ALL_CAPS_SNAKE_CASE` for macros, enum constants, and compile-time
  constants.
- Use descriptive booleans such as `isReady`, `hasRom`, or `shouldTrace` for
  new code.
- Avoid leading underscores for project-owned names. Those names are reserved
  by C and system headers in many contexts.

## Types

- In decompiled game code, use the project's existing fixed-width aliases
  (`u8`, `s32`, `f32`, and similar) when surrounding code does.
- In platform code, follow the local file's convention. Standard fixed-width
  types from `<stdint.h>` are fine where the platform layer already uses them.
- Keep casts explicit at platform boundaries, especially when converting between
  emulated N64 addresses and native pointers.

## Headers And Scope

- Header files declare public functions, types, constants, and `extern`
  variables. They should not define storage for mutable globals.
- Exactly one source file should define each public global.
- File-local helper functions and file-local globals should be `static`.
- Do not add broad include directories or wildcard source globs that pull SDK or
  compatibility files into native builds. Native SDK surface boundaries are
  release-guarded.

## Decompiled Game Code

- Prefer behavior-preserving, reviewable changes. Do not refactor decompiled
  game logic just for taste.
- If a port issue can be fixed in `src/platform/`, prefer that over changing
  original-game logic.
- When a native-port fix must touch game code, keep it narrow and explain the
  port-specific reason in the commit or PR.
- For matching work, document known mismatches and use the existing diff tooling
  (`scripts/asmdiff.sh` and related helpers).

## Native Port Code

- Keep platform-owned behavior in `src/platform/`, `macos/`, or other explicit
  port seams.
- Keep binaries asset-free. Game media must be loaded from the user's ROM at
  runtime, never compiled into native executables or app bundles.
- Prefer small diagnostics and validation hooks that can be run by maintainers
  without committing captured ROM-derived screenshots, audio, or traces.
- When adding environment variables, command-line flags, or trace fields,
  document them in `docs/INSTRUMENTATION.md` if contributors are expected to use
  them.

## Comments

- Use comments to explain non-obvious compatibility, matching, endian, timing,
  or platform-boundary decisions.
- Avoid comments that simply restate the next line of code.
- Keep provenance notes in the public provenance docs when possible instead of
  scattering historical source-path notes through code.

## Performance Discipline (renderer)

The per-vertex and per-triangle code paths run tens of thousands of times per
frame on a single thread; that thread is the frame's critical path. See
`docs/RENDERING_ARCHITECTURE.md` for the pipeline and two cautionary case studies.

- **Nothing runs per-primitive unless it draws that primitive.** Keep diagnostics,
  room/effect attribution, provenance/`dbg_*` bookkeeping, and string matching out
  of per-vertex / per-triangle loops. Hoist to per-command, memoize on a stable
  key, or gate behind a latched flag so it costs nothing when inactive.
- **A per-level feature must not tax the other levels.** Give perf-sensitive
  features a scope, a budget, and a `GE007_*` A/B escape hatch (default-on-global
  is how the two shipped render defects happened).
- **Framebuffer readback and GL state churn are batch-level, never per-primitive.**
- **Measure.** Run `tools/perf_census.sh` before/after any renderer change and keep
  every level within `docs/design/PERFORMANCE_PLAN.md` §6 (60 fps hard floor, 120 fps
  target). The opt-in `port_perf_budget_smoke` CTest lane enforces it.

## Pull Request Expectations

- Keep changes focused by subsystem.
- Include the validation command that proves the change, or explain why the
  change is documentation-only.
- Re-read added files for accidental ROM-derived data, local paths, and private
  notes before opening the PR.
