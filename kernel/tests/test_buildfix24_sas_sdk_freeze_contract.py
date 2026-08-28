#!/usr/bin/env python3
from pathlib import Path
import sys

r = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]

def req(value, message):
    if not value:
        raise SystemExit("titan-sas-sdk-freeze-contract: FAIL: " + message)

# SAS remains a system/runtime capability in the main tree.
req((r / "kernel/src/services/titan_sas.c").is_file(), "Titan SAS implementation missing")
req((r / "kernel/src/services/titan_sas.h").is_file(), "Titan SAS header missing")

sas_c = (r / "kernel/src/services/titan_sas.c").read_text(encoding="utf-8", errors="replace")
sas_h = (r / "kernel/src/services/titan_sas.h").read_text(encoding="utf-8", errors="replace")

# Do not freeze against deleted historical reports.  Developer-facing SAS
# guidance moved with the standalone SDK; this repository only needs the
# runtime implementation and the documented distribution boundary.
policy = " ".join(
    (r / "docs/reference/TITAN_STANDALONE_SDK.md")
    .read_text(encoding="utf-8", errors="replace")
    .split()
)

req("distributed separately" in policy, "standalone SDK distribution boundary missing")
req("0.3.93-rc17" in policy, "validated runtime baseline missing")
req(not (r / "sdk/titan-app").exists(), "embedded Titan App SDK returned")
req(not (r / "sdk/titan-game").exists(), "embedded Titan Game SDK returned")
req(not (r / "docs/TITAN_APP_SDK.md").exists(), "old embedded App SDK doc returned")
req(not (r / "docs/TITAN_GAME_SDK.md").exists(), "old embedded Game SDK doc returned")

# Minimal semantic sanity: this really is the SAS runtime, not just empty
# placeholder files. Keep this deliberately token-based instead of tying the
# release to archival filenames or prose formatting.
combined = sas_c + "\n" + sas_h
req("TitanSas" in combined or "TITAN_SAS" in combined, "SAS runtime symbols missing")

print("titan-sas-sdk-freeze-contract: OK runtime retained; developer docs live in standalone SDK")
