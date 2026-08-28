# pikaVR 720p Target Readiness — BuildFix24 CP06

## Policy
- Release/default path: OPTIMIZED 960x540 composition -> 1280x720 output (hardware-approved BF30 policy).
- Optimized 960x540: hardware-approved release/default 720p composition path; Reference remains available in DEX for A/B diagnostics and emergency fallback.
- Output remains 1280x720p for both paths.
- Reserved headroom policy remains 524288 bytes for optimized composition.

## A/B capture procedure
For each screen below, select Reference, export `mass:/TITAN/debug/titan-debug.txt`, capture visual evidence, then select Optimized 960x540 and repeat without rebuilding. Record: internal/output resolution, VRAM used/free/reserved, texture residency, cache/palettization/dedupe counters, frame time/P95/max and scale mode.

## Screen test matrix
| Screen | Sharp text/small glyphs | Icons/thin borders/focus | Ellipsis/clipping/safe area | Waves/Ribbon/theme layers | Gaps/flicker/tearing | Ref | Opt |
|---|---|---|---|---|---|---|---|
| Welcome | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Profile Selection | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Character/Avatar Selection | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| HOME | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| TOP Menu | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Settings | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Library | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Applications | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Package Installer | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Notifications | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Theme selection | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Saved Data/VMC | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| Overview/Information | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| appdatahome | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| ftphome | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| DEX overlay | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| dialogs | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| keyboard | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| download/install UI | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |

No unchecked target item is a PASS. PCSX2 and PS2 hardware results must be recorded separately.
