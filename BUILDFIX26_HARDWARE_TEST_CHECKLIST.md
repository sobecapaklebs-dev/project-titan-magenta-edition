# BuildFix26 Hardware Test Checklist

Status before physical execution: **HARDWARE_VALIDATED = NO / NOT_EXECUTED**.

Keep the known-functional rollback media/kernel available throughout the test. BuildFix26 cold boot must remain 720p Reference when 720p is enabled; do not select Optimized until Phase D.

## Phase A — cold boot / Reference safety

1. Boot the exact R5900 artifact produced from the BuildFix26 final source.
2. Confirm video lock and no GS corruption.
3. Confirm Welcome, Profile and HOME are reached.
4. Export Debug Report immediately.
5. Record `BOOT VRAM HISTORY`, recovery mode, GS used/free/min-free/peak, EE validity/used/free/peak/min-free, and IOP validity/module count.
6. FAIL if optional artwork prevents HOME; minimum fallback reaching HOME is acceptable and must be logged.
7. FAIL if Reference is not the cold-boot 720p path.

Expected failure signatures:
- `recovery=MINIMUM` + HOME reached: degraded boot, diagnostic PASS for survivability but investigate pressure.
- critical allocation failure + HOME not reached: FAIL; record last phase/resource/size.
- `used > total`, wrapped free value, or invalid GS handles: FAIL immediately.

## Phase B — shell + theme lifecycle, 10 cycles

For cycles 1..10 execute:
`HOME -> Settings -> HOME -> Library -> HOME -> Applications -> HOME -> Theme selector -> change theme -> HOME`.

Export one Debug Report per cycle. Run on host:

```sh
python3 kernel/tools/titan_lifecycle_watermarks.py cycle01.txt ... cycle10.txt
```

FAIL on unjustified monotonic growth without plateau in VRAM, valid EE heap estimate, valid IOP RAM, texture residency or theme resident frames. Bounded caches may warm up then stabilize.

Theme stress order:
`default -> complex A -> complex B -> minimum/reduced -> default`.
The current theme working set must not become “all declared frames resident”; CURRENT/NEXT/RECENT remains bounded.

## Phase C — Network / Storage / TitanEYE lifecycle

1. Enable the known-good Network Core path and validate AR9271/Wi-Fi; validate Ethernet if hardware is available.
2. Confirm DHCP/DNS/connectivity and one controlled asset/network operation.
3. Scan Library while UI remains active; observe audio/render responsiveness and storage scheduler.
4. Launch TitanEYE from HOME.
5. Confirm shell foreground resources transition to game mode.
6. Exercise gameplay/runtime sufficiently to hit TGR texture/viewport paths.
7. Return to Titan.
8. Confirm shell renderer, Theme, Ribbon, audio, Network and Storage state restore.
9. Export Debug Report after return.
10. Repeat launch/return as part of the 10-cycle lifetime dataset when practical.

FAIL if return leaves stale renderer/path, monotonic texture growth, storage exclusive ownership stuck, Network broken, or HOME unable to redraw.

## Phase D — 720p Optimized A/B (only after A-C pass)

1. DEX -> Graphics -> 720p Composition -> verify Reference.
2. Capture Reference report and visual notes.
3. Switch Reference -> Optimized 960x540.
4. Inspect text, thin lines, focus rectangle, Ribbon, Theme layers, clipping, border gaps, shimmering, tearing and frame pacing.
5. Capture Optimized report.
6. Switch back Optimized -> Reference and capture report.
7. Repeat switching under normal theme and a complex theme.
8. Test invalid/corrupted preference via the validation mechanism if available; expected active path is Reference.

Promotion is forbidden unless hardware evidence shows useful headroom, acceptable UI/text quality, no GS corruption, no severe frame-time regression and stable lifecycle. Otherwise Reference remains default.

## Report bundle to return for engineering

- exact ELF SHA-256 and target-build report directory;
- first cold-boot Debug Report;
- 10 lifecycle Debug Reports;
- Reference/Optimized A/B reports;
- photos/screenshots of visual anomalies if any;
- model/region/output cable or converter information;
- network/storage devices used;
- exact failure phase and last visible frame for any crash.
