#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2])
ksvc = root / "kernel/src/services"
shell = (root / "kernel/src/ui/titan_shell_scene.c").read_text(encoding="utf-8")
neutrino = (ksvc / "titan_neutrino.c").read_text(encoding="utf-8")
gsm = (ksvc / "titan_game_gsm.c").read_text(encoding="utf-8")
makefile = (root / "kernel/Makefile").read_text(encoding="utf-8")

# SELECT UX is a compact two-column Resolution options flow, not inline cycling.
assert "TITAN_CONTEXT_ACTION_RESOLUTION_OPTIONS" in shell
assert '"Resolution options"' in shell
assert "resolution_options_open" in shell
assert "resolution_options_selected" in shell
assert "resolution_options_transition" in shell
assert "TitanShellScene_OpenResolutionOptions" in shell
assert "TitanShellScene_SetSelectedGameResolution" in shell
draw_context = shell.split("static void TitanShellScene_DrawFolderContextMenu(void)", 1)[1].split(
    "static void TitanShellScene_DrawFolderScreenBackdrop", 1
)[0]
assert "const float resolution_width = 184.0f" in draw_context
assert "const float patch_width = 300.0f" in draw_context
assert "float option_width = 184.0f;" in draw_context
assert "if (patch_transition > resolution_transition)" in draw_context
assert "option_width = patch_width;" in draw_context
assert "main_width = 286.0f" in draw_context
assert "option_x = panel_x + main_width" in draw_context
assert "option_visible_width = option_width * submenu_transition" in draw_context
assert "(float)TITAN_UI_HEIGHT" in draw_context
assert "outer_right" not in draw_context
assert "const float gap" not in draw_context
assert "main_y" not in draw_context
assert "option_y" not in draw_context
assert "de_emphasis" in draw_context
assert "TITAN_CONTEXT_ACTION_GAME_GSM" not in shell
assert '"GSM: < %s >"' not in shell
assert 'labels[3] = "Resolution options";' in shell

# Neutrino modes deliberately omit 720p; Titan UL keeps it.
mode_fn = shell.split("static unsigned int TitanShellScene_BuildResolutionModes", 2)[2].split("static int TitanShellScene_GetSelectedResolutionGame", 1)[0]
assert "TITAN_PER_GAME_GSM_DEFAULT" in mode_fn
assert "TITAN_PER_GAME_GSM_480P" in mode_fn
assert "TITAN_NEUTRINO_GAME_USBEXTREME" in mode_fn
assert "TITAN_PER_GAME_GSM_720P" in mode_fn
assert "TITAN_PER_GAME_GSM_1080I" in mode_fn

# Persistence stays per-title and atomic.
assert 'TITAN_STORAGE_DEVICE_ROOT "/games"' in gsm
assert '"/%s.gsm%s"' in gsm
assert "TitanStorageIo_AtomicReplace" in gsm
assert "encoded = (char)('0' + (int)mode);" in gsm
assert "src/services/titan_game_gsm.o" in makefile

# Golden Neutrino only receives parser modes it already understands.
assert 'case TITAN_PER_GAME_GSM_480P: return "-gsm=fp2";' in gsm
assert 'case TITAN_PER_GAME_GSM_1080I: return "-gsm=1080ix2";' in gsm
assert "case TITAN_PER_GAME_GSM_720P:" in gsm
assert 'return "-gsm=720p";' not in gsm
assert 'return "-gsm=480p";' not in gsm

# Golden Titan UL is not extended through argv or engine source. Kernel writes
# normal OPL CFG keys and preserves the proven 3-argument handoff.
assert "TitanGameGsm_PrepareUlGoldenConfig" in gsm
for key in ["$GSMSource=1", "$EnableGSM=0", "$EnableGSM=1", "$GSMVMode=%d"]:
    assert key in gsm
assert "case TITAN_PER_GAME_GSM_480P: vmode = 8" in gsm
assert "case TITAN_PER_GAME_GSM_720P: vmode = 10" in gsm
assert "case TITAN_PER_GAME_GSM_1080I: vmode = 11" in gsm
assert "TitanGameGsm_PrepareUlGoldenConfig" in neutrino
assert 'plan->arguments[plan->argument_count++] = g_opl_ul_loader;' in neutrino
assert 'plan->arguments[plan->argument_count++] = (char *)game->startup;' in neutrino
assert 'plan->arguments[plan->argument_count++] = "titan-ul";' in neutrino
ul_branch = neutrino.split("if (game->format == TITAN_NEUTRINO_GAME_USBEXTREME)",1)[1].split("if (g_neutrino_elf[0]",1)[0]
assert '"-gsm=' not in ul_branch

print("TITAN_GAME_GSM_GOLDEN_RESOLUTION_CONTRACT=PASS")
