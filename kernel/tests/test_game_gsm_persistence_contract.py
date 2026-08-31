#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2])
gsm = (root / "kernel/src/services/titan_game_gsm.c").read_text(encoding="utf-8")

assert "TitanGameGsm_EnsureDirectoryPath" in gsm
assert "TitanStorageIo_PathInfo64(path, &is_directory, 0)" in gsm
assert "TitanStorageIo_CreateDirectory(path) == 0" in gsm
assert "FileXio does not reliably mirror IOP errno into EE errno" in gsm

ensure = gsm.split("static int TitanGameGsm_EnsureDirectoryPath", 1)[1].split(
    "static int TitanGameGsm_EnsureDirectory(void)", 1
)[0]
assert "errno != EEXIST" not in ensure
assert "errno == EEXIST" not in ensure

assert "TITAN_GAME_GSM_SAVE_STAGE_DIRECTORY" in gsm
assert "TITAN_GAME_GSM_SAVE_STAGE_OPEN" in gsm
assert "TITAN_GAME_GSM_SAVE_STAGE_WRITE" in gsm
assert "TITAN_GAME_GSM_SAVE_STAGE_REPLACE" in gsm
assert "TITAN_GAME_GSM_SAVE_STAGE_COMPLETE" in gsm
assert "TITAN_DIAG_STORAGE" in gsm
assert "0x0341u" in gsm

print("TITAN_GAME_GSM_PERSISTENCE_PHYSICAL_FIX_CONTRACT=PASS")
