#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
scheduler = (root / "src/services/titan_storage_scheduler.c").read_text()
assets = (root / "src/services/titan_asset_service.c").read_text()
background = (root / "src/ui/titan_shell_background.c").read_text()


def require(condition, message):
    if not condition:
        raise AssertionError(message)


worker = scheduler[scheduler.index("static void TitanStorageScheduler_Worker(void *argument)"):]
worker_scan = worker.index("if (TitanStorageScheduler_StartOrStepScan())")
worker_asset_turn = worker.index("if (g_storage_scheduler.asset_scan_turn &&")
require(worker_asset_turn < worker_scan,
        "worker must service the artwork turn before stepping the library scan")
worker_asset_block = worker[worker_asset_turn:worker_scan]
require("TitanAssetService_StorageWorkerPump()" in worker_asset_block,
        "worker artwork turn must pump the asset service")
require("g_storage_scheduler.asset_scan_turn = 0;" in worker_asset_block,
        "worker must return the next shared turn to the scan")
require("TitanAssetService_HasStorageWork() ? 1 : 0" in worker[worker_scan:worker_scan + 500],
        "scan step must yield next turn when artwork is waiting")
require("TitanLibraryScan_Update(2u);" in scheduler,
        "library scan must make bounded multi-entry progress per turn")

fetch_abort = assets[assets.index("static int TitanAssetService_ShouldAbortFetch(void *user_data)"):assets.index("static void TitanAssetService_ImageYield")]
decode_start = assets.index("static int TitanAssetService_ShouldAbortExternalDecode(void *user_data)")
decode_end = assets.index("static int TitanAssetService_ProcessExternalDecode(void)\n{", decode_start)
decode_abort = assets[decode_start:decode_end]
for abort_guard, owner in ((fetch_abort, "HTTP fetch"), (decode_abort, "local decode")):
    require("TitanStorageScheduler_IsLibraryScanBusy()" not in abort_guard,
            f"library scan must not repeatedly cancel {owner}")
    require("TitanStorageScheduler_IsForegroundBusy()" in abort_guard,
            f"foreground-exclusive work must still preempt {owner}")

fallback = scheduler[scheduler.index("static void TitanStorageScheduler_RunFallbackWork(void)"):]
fallback_scan = fallback.index("if (TitanStorageScheduler_StartOrStepScan())")
fallback_asset_turn = fallback.index("if (g_storage_scheduler.asset_scan_turn &&")
require(fallback_asset_turn < fallback_scan,
        "fallback scheduler must also service artwork before the next scan step")
require("TitanAssetService_StorageWorkerPump()" in fallback[fallback_asset_turn:fallback_scan],
        "fallback artwork turn must pump the asset service")

render = background[background.index("void TitanShellBackground_Draw("):]
minimum = render[render.index("BuildFix25 boot-safety"):render.index(
    "if (TitanTheme_GetActive()->background_engine")]
require(minimum.index("TitanXmbSpline_DrawWithOpacity") <
        minimum.index("TitanShellBackground_DrawAtmosphere"),
        "minimum fallback must draw shell atmosphere above the Flow ribbon")
flow = render[render.rindex("transition = TitanShellBackground_Clamp01(page_transition);"):]
require(flow.index("TitanXmbSpline_Draw(") <
        flow.index("TitanShellBackground_DrawAtmosphere") <
        flow.index("TitanShellBackground_DrawParticles"),
        "normal Flow must layer ribbon below atmosphere and particles")
v4 = render[render.index("if (TitanTheme_GetActive()->background_engine == TITAN_THEME_BACKGROUND_SCENE_V4"):]
v4_flow = v4[v4.index("if (ribbon_opacity > 0.001f"):v4.index("if (g_scene_v4_ready")]
require(v4_flow.index("TitanXmbSpline_DrawWithOpacity") <
        v4_flow.index("TitanShellBackground_DrawAtmosphere") <
        v4_flow.index("TitanShellBackground_DrawParticles"),
        "V4 transition must layer ribbon below atmosphere and scene")
v2 = render[render.index("if (TitanTheme_GetActive()->background_engine == TITAN_THEME_BACKGROUND_SCENE_V2"):]
v2_flow = v2[v2.index("if (system_amount > 0.001f"):v2.index("scene_alpha =")]
require(v2_flow.index("TitanXmbSpline_DrawWithOpacity") <
        v2_flow.index("TitanShellBackground_DrawAtmosphere") <
        v2_flow.index("TitanShellBackground_DrawParticles"),
        "V2 system transition must layer ribbon below atmosphere and scene")

print("storage-artwork-fairness: PASS scan/artwork alternation in worker and fallback")
print("storage-artwork-fairness: PASS library scan no longer cancels asset fetch/decode")
print("storage-artwork-fairness: PASS shell atmosphere layered above Flow ribbon")
