#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
trace = (root / "kernel/src/services/titan_health_trace.c").read_text(encoding="utf-8")
header = (root / "kernel/src/services/titan_health_trace.h").read_text(encoding="utf-8")
scene = (root / "kernel/src/ui/titan_shell_scene.c").read_text(encoding="utf-8")

def req(ok, msg):
    if not ok:
        raise SystemExit("FAIL: " + msg)

req("void TitanHealthTrace_SetEnabled(int enabled)" in trace, "trace mute API missing")
req("int TitanHealthTrace_IsEnabled(void)" in trace, "trace state query missing")
req("if (!g_health_trace.initialized || !g_health_trace_enabled)" in trace, "mute guard missing")
req("if (sequence == 0u)\n        return;" in trace, "Critical still waits while muted")
req("g_health_trace_enabled &&" in trace, "storage worker can flush while muted")
req("void TitanHealthTrace_SetEnabled(int enabled);" in header, "header mute declaration missing")
health = scene.index("g_scene.startup_phase = TITAN_STARTUP_PHASE_HEALTH;")
disable = scene.index("TitanHealthTrace_SetEnabled(0);", health)
health_critical = scene.index("TitanHealthTrace_Critical(TITAN_HT_STARTUP_HEALTH_TRANSITION", health)
welcome = scene.index("g_scene.startup_phase = TITAN_STARTUP_PHASE_WELCOME_ENTER;", health)
enable = scene.index("TitanHealthTrace_SetEnabled(1);", welcome)
req(health < disable < health_critical, "trace must mute before H&S Critical")
req(welcome < enable, "trace must resume after leaving H&S")
req("TitanHealthTrace_Mark(TITAN_HT_STARTUP_HEALTH_FRAME" in scene, "H&S marker call unexpectedly deleted")
print("RC5_6_HEALTH_SCREEN_TRACE_QUIET_CONTRACT=PASS")
