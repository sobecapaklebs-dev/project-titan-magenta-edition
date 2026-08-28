# MGB64 documentation

Reference for building, understanding, and contributing to the port. If you're
new, start with **[BUILDING.md](BUILDING.md)** and
**[APP_ARCHITECTURE.md](APP_ARCHITECTURE.md)**.

For the project overview and download links, see the top-level
[README](../README.md); for how to contribute, see
[CONTRIBUTING](../CONTRIBUTING.md) and the branch → PR → merge
[WORKFLOW](WORKFLOW.md).

## Start here

| Doc | What it covers |
| --- | --- |
| [BUILDING.md](BUILDING.md) | The two build targets (desktop port via CMake; N64 ROM-matching via Makefile), toolchain, and asset extraction from **your** ROM. |
| [APP_ARCHITECTURE.md](APP_ARCHITECTURE.md) | How the in-process app, engine, and platform layer fit together — the big picture, per-OS seams, and invariants. |
| [CODING_STYLE.md](CODING_STYLE.md) | Naming, headers, scope, and — importantly — the decompiled-code vs. port-layer boundary. |
| [STATUS.md](STATUS.md) | Honest current state: what works, what's experimental, known limitations. |
| [WINDOWS_SETUP.md](WINDOWS_SETUP.md) | Installing and running the Windows portable build: install location, adding your ROM, save/config locations, controller/launcher notes, and logs. |

## Architecture & internals

| Doc | What it covers |
| --- | --- |
| [RENDERING_ARCHITECTURE.md](RENDERING_ARCHITECTURE.md) | The Fast3D display-list interpreter, the GL/Metal backend seam, and how N64 GBI becomes modern GPU calls. |
| [FRAME_TIMING_ARCHITECTURE.md](FRAME_TIMING_ARCHITECTURE.md) | The fixed 60 Hz integer-tick simulation, frame pacing, and why gameplay is locked to 60. |
| [RENDERING_REGRESSION_NOTES.md](RENDERING_REGRESSION_NOTES.md) | Renderer failure modes already fixed once — read before touching the render path so they don't come back. |

## Configuration, features & validation

| Doc | What it covers |
| --- | --- |
| [VISUAL_MODES.md](VISUAL_MODES.md) | The faithful / remaster visual modes and the flags that select them. |
| [INSTRUMENTATION.md](INSTRUMENTATION.md) | The validation lanes (save/pixel/state/audio), the trace schema, and the diagnostic environment variables. |
| [ENV_FLAGS.md](ENV_FLAGS.md) | Generated reference of every `GE007_*` environment flag scanned from the source (regenerate with `tools/gen_env_reference.py`; a CTest gate keeps it current). |
| [CINEMATICS.md](CINEMATICS.md) | The animated mission intro/outro and death cameras: what plays, the settings that shape it, and how to validate faithfulness. |

## Porting, release & provenance

| Doc | What it covers |
| --- | --- |
| [PORTING_AND_EXPANSION.md](PORTING_AND_EXPANSION.md) | Guidance for extending the port and adding platform support. |
| [RELEASING.md](RELEASING.md) | The release process and pipeline. |
| [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) | The step-by-step pre-release checklist. |
| [PROVENANCE_AUDIT.md](PROVENANCE_AUDIT.md) | Provenance/legal audit — what's first-party vs. third-party, and the no-ROM-data discipline. |
| [ROM_COMPARISON.md](ROM_COMPARISON.md) | Notes on comparing the port's behavior against the original ROM. |

## Design notes (internal)

Forward-looking design and roadmap notes — rendering, audio, multiplayer/netplay,
HD assets, gameplay-feel — live in the development tree under `docs/design/`.
They record intent and rationale for in-progress or planned work and are useful
background for it, but they are **internal working notes**: not part of the
public reference, not onboarding material, and not shipped in release archives
(they are `export-ignore`d — see [`.gitattributes`](../.gitattributes)). Treat
them as design history, not current-state documentation.
