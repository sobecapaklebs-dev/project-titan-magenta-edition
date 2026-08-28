#!/usr/bin/env python3
"""ROM-free regression checks for native TRI-PIXEL probe summaries."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

import summarize_native_tri_pixel_probe as tri


def row(frame: int,
        tri_id: int,
        texnum: int,
        pre: list[int],
        post: list[int],
        changed: int) -> dict[str, object]:
    return {
        "status": "ok",
        "frame": frame,
        "tri": tri_id,
        "serial": tri_id,
        "target": [94, 95],
        "inside": 1,
        "changed": changed,
        "drawclass": "room",
        "dl_room": 132 if texnum != 654 else -1,
        "dl": "primary" if texnum != 654 else "-",
        "settex": 1,
        "texnum": texnum,
        "wh": [54, 54],
        "cc": "0x00738e4f020a2d12" if texnum == 654 else "0x009ffe4f0ebe2d12",
        "effcc": "0x00738e4f020a2d12" if texnum == 654 else "0x009ffe4f0ebe2d12",
        "raw": "0xC41049D8" if texnum == 654 else "0xC8102078",
        "effmode": "0xC41049D8" if texnum == 654 else "0xC8102078",
        "blend": "alpha" if texnum == 654 else "disabled",
        "api_blend": "alpha" if texnum == 654 else "disabled",
        "depth": {"zmode": "xlu" if texnum == 654 else "opa", "zraw": "0x800"},
        "mode": {
            "cvg": "wrap" if texnum == 654 else "clamp",
            "force_bl": 1 if texnum == 654 else 0,
            "clr_on_cvg": 1 if texnum == 654 else 0,
            "fog": 1,
            "fog_fixed": 1 if texnum == 654 else 0,
            "roommtx": 0 if texnum == 654 else 1,
            "sky": 0,
        },
        "rect": {"op": "none"},
        "pre": pre,
        "post": post,
        "delta": [post[index] - pre[index] for index in range(3)],
        "screen_bbox": [1.0, 2.0, 3.0, 4.0],
    }


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_native_tri_pixel_probe_") as tmp:
        path = Path(tmp) / "native.log"
        records = [
            row(122, 673, 949, [42, 71, 113], [11, 11, 11], 1),
            row(122, 787, 654, [11, 11, 11], [7, 7, 7], 1),
            row(122, 790, 654, [7, 7, 7], [8, 8, 8], 1),
            row(123, 790, 654, [7, 7, 7], [9, 9, 9], 1),
        ]
        with path.open("w", encoding="utf-8") as handle:
            for record in records:
                handle.write("[TRI-PIXEL] " + json.dumps(record) + "\n")
        payload = tri.summarize(tri.load_rows(path), (94, 95), 122, False)

    assert payload["status"] == "pass"
    assert payload["total_rows"] == 4
    assert payload["filtered_rows"] == 3
    assert payload["changed_rows"] == 3
    assert payload["frames"] == [{"frame": 122, "records": 3}]
    assert payload["texnums"][0] == {"texnum": 654, "records": 2}
    assert payload["rows"][0]["pre"] == [42, 71, 113]
    assert payload["rows"][-1]["post"] == [8, 8, 8]
    assert payload["rows"][1]["mean_abs_delta"] == 4.0
    assert any("[42, 71, 113] -> [8, 8, 8]" in item for item in payload["interpretation"])

    print("PASS: native TRI-PIXEL probe summary regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
