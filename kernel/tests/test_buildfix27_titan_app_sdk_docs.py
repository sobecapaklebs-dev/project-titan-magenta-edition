#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
def req(v,m):
    if not v: raise SystemExit("titan-standalone-sdk-separation: FAIL: "+m)
app=(r/"kernel/include/titan_app_sdk.h").read_text()
game=(r/"kernel/include/titan_game_sdk.h").read_text()
policy=(r/"docs/reference/TITAN_STANDALONE_SDK.md").read_text()
req("#define TITAN_APP_API_VERSION 1u" in app,"Titan App ABI v1 missing")
req("#define TITAN_GAME_API_VERSION 2u" in game,"TGR ABI v2 missing")
req("0.3.93-rc17" in policy and "FINAL-R2" in policy,"validated standalone baseline missing")
req("R5900 N32 / EF_MIPS_ABI2" in policy,"N32 ABI policy missing")
for p in ("sdk/titan-app","sdk/titan-game","docs/TITAN_APP_SDK.md","docs/TITAN_GAME_SDK.md"):
    req(not (r/p).exists(),"embedded SDK artifact remains: "+p)
print("titan-standalone-sdk-separation: OK")
