#!/usr/bin/env python3
"""ROM-free regression checks for RDP render-mode trace summaries."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import summarize_rdp_render_mode_trace as rdp_modes


SURFACE_PROMOTED = (
    "[RDP-MODE] frame=1 tri=80 cmd=0x1 class=room effect=- roommtx=1 "
    "dl_room=18 dl=secondary offset=0x60 raw=0xC81049D8 eff=0xC81049D8 "
    "omh=0x00992C20 geom=0x00012205 cc=0x00F78E4F0EBE2D12 "
    "effcc=0x00F78E4F0EBE2D12 opts=0x00403012 settex=1 texnum=1264 "
    "wh=32x32 tex_used=(1,1) envA=255 primA=180 fogA=255 "
    "blend=alpha api_blend=alpha_rdp_cvg_memory "
    "rdp_mem=coverage room_cvg=1 water_suppress=0 room_sort=0 alpha=1 "
    "room_cvg_reason=ok fog=1 texedge=0 noise=0 "
    "depth=(test=1,upd=0,cmp=1,prim=0,z=xlu) "
    "decode={z=xlu cvg=wrap zcmp=1 zupd=0 aa=1 imrd=1 clr_on_cvg=1 "
    "cvg_x_alpha=0 alpha_cvg=0 force_bl=1 b1=(0,3,0,2) b2=(0,0,1,0)} "
    "ndc_ok=1 bbox=[0.1,0.2,0.3,0.4] area2=0.01\n"
)


DAM_UNPROMOTED = (
    "[RDP-MODE] frame=122 tri=815 cmd=0x2 class=effect effect=glass_shards "
    "roommtx=0 dl_room=-1 dl=? offset=?0x0 raw=0x0C1849D8 eff=0x0C1849D8 "
    "omh=0x00992C60 geom=0x00060205 cc=0x00F38E4F020A2D12 "
    "effcc=0x00F38E4F020A2D12 opts=0x00000511 settex=0 texnum=-1 "
    "wh=0x0 tex_used=(1,1) envA=255 primA=255 fogA=255 "
    "blend=alpha api_blend=alpha rdp_mem=none "
    "room_cvg=0 water_suppress=0 room_sort=0 alpha=1 fog=0 texedge=0 "
    "room_cvg_reason=class noise=0 depth=(test=1,upd=0,cmp=1,prim=0,z=xlu) "
    "decode={z=xlu cvg=wrap zcmp=1 zupd=0 aa=1 imrd=1 clr_on_cvg=1 "
    "cvg_x_alpha=0 alpha_cvg=0 force_bl=1 b1=(0,3,0,2) b2=(0,0,1,0)} "
    "ndc_ok=1 bbox=[0.2,-0.8,0.3,-0.7] area2=0.003\n"
)


JUNGLE_GENERATED_PROMOTED = (
    "[RDP-MODE] frame=220 tri=646 cmd=0x3 class=room effect=- roommtx=0 "
    "dl_room=-1 dl=? offset=?0x0 raw=0xC41049D8 eff=0xC41049D8 "
    "omh=0x00992C60 geom=0x00012205 cc=0x00738E4F020A2D12 "
    "effcc=0x00738E4F020A2D12 opts=0x00443C13 settex=1 texnum=2493 "
    "wh=32x32 tex_used=(1,1) envA=255 primA=0 fogA=255 "
    "blend=alpha api_blend=alpha_rdp_cvg_memory "
    "rdp_mem=coverage room_cvg=1 water_suppress=0 room_sort=0 "
    "room_cvg_reason=ok_generated_room alpha=1 fog=1 texedge=0 noise=0 "
    "depth=(test=1,upd=0,cmp=1,prim=0,z=xlu) "
    "decode={z=xlu cvg=wrap zcmp=1 zupd=0 aa=1 imrd=1 clr_on_cvg=1 "
    "cvg_x_alpha=0 alpha_cvg=0 force_bl=1 b1=(3,1,0,0) b2=(0,0,1,0)} "
    "ndc_ok=1 bbox=[-0.110,0.044,-0.031,0.096] area2=0.00084\n"
)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_rdp_mode_summary_") as tmp:
        path = Path(tmp) / "native.log"
        path.write_text(
            SURFACE_PROMOTED + DAM_UNPROMOTED + JUNGLE_GENERATED_PROMOTED,
            encoding="utf-8",
        )
        rows = rdp_modes.load_rows([path])

        assert len(rows) == 3
        assert rows[0]["api_blend"] == "alpha_rdp_cvg_memory"
        assert rows[0]["rdp_mem"] == "coverage"
        assert rows[0]["room_cvg"] == "1"
        assert rows[0]["roommtx"] == "1"
        assert rows[0]["dl_room"] == "18"
        assert rows[0]["dl"] == "secondary"
        assert rows[0]["envA"] == "255"
        assert rows[0]["primA"] == "180"
        assert rows[0]["room_cvg_reason"] == "ok"
        assert not rdp_modes.is_unpromoted_coverage_candidate(rows[0])
        assert rows[1]["class"] == "effect"
        assert rows[1]["effect"] == "glass_shards"
        assert rows[1]["raw"] == "0x0C1849D8"
        assert rows[1]["api_blend"] == "alpha"
        assert rows[1]["room_cvg_reason"] == "class"
        assert rdp_modes.is_unpromoted_coverage_candidate(rows[1])
        assert rows[2]["api_blend"] == "alpha_rdp_cvg_memory"
        assert rows[2]["rdp_mem"] == "coverage"
        assert rows[2]["roommtx"] == "0"
        assert rows[2]["dl_room"] == "-1"
        assert rows[2]["room_cvg_reason"] == "ok_generated_room"
        assert rows[2]["envA"] == "255"
        assert rows[2]["primA"] == "0"
        assert not rdp_modes.is_unpromoted_coverage_candidate(rows[2])
        payload = rdp_modes.summarize(rows, top=4)
        assert payload["promoted_coverage_memory_rows"] == 2
        assert payload["unpromoted_coverage_candidate_rows"] == 1
        assert payload["counts"]["api_blend"] == {"alpha": 1, "alpha_rdp_cvg_memory": 2}
        assert payload["counts"]["room_cvg_reason"] == {
            "class": 1,
            "ok": 1,
            "ok_generated_room": 1,
        }

        json_path = Path(tmp) / "summary.json"
        assert rdp_modes.main([str(path), "--top", "4", "--json-out", str(json_path)]) == 0
        saved = json.loads(json_path.read_text(encoding="utf-8"))
        assert saved["promoted_coverage_memory_rows"] == 2
        assert saved["unpromoted_coverage_candidate_rows"] == 1
        assert rdp_modes.main([str(path), "--top", "4"]) == 0

    print("PASS: RDP render-mode trace summary regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
