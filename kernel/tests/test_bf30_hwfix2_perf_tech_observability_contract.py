#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
scene = (root/'kernel/src/ui/titan_shell_scene.c').read_text(errors='replace')
checks = {
    'system-exposes-pikavr': 'labels[14] = "pikaVR";' in scene and 'TitanVideo_GetPikaVrTelemetry(&pikavr_telemetry);' in scene,
    'system-exposes-vlombis': 'labels[15] = "VlombisCore";' in scene and 'TitanVlombisCore_GetTelemetry(&vlombis_telemetry);' in scene,
    'system-row-count-expanded': 'case TITAN_SETTINGS_SYSTEM:\n            return 16;' in scene,
    'dex-pika-live': '"PIKA %s  H/M %u/%u"' in scene and 'pikavr.vram_saved / 1024u' in scene,
    'dex-vlombis-live': '"VLOM %s %u/10 P%u E%u"' in scene and 'TitanVlombisCore_GetActiveCapabilityCount()' in scene,
    'debug-report-pikavr': '"pikaVR: %s api=%u optimized=%u\\n"' in scene and 'TITAN_PIKAVR_API_VERSION' in scene,
    'debug-report-vlombis': '"VlombisCore: %s api=%u caps=0x%03X active_domains=%u/10 measured=%u pressure=%u\\n"' in scene and '"Vlombis library: cache_hits=%u scan_steps=%u metadata_reused=%u avoided_bytes=%u\\n"' in scene,
    'truthful-vlombis-ready-active': 'vlombis.measured_events != 0u ? "ACTIVE" : "READY"' in scene,
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f'bf30-perf-tech-observability: {"PASS" if ok else "FAIL"} {name}')
if failed:
    raise SystemExit('failed: ' + ', '.join(failed))
print(f'bf30-perf-tech-observability: PASS {len(checks)}/{len(checks)}')
