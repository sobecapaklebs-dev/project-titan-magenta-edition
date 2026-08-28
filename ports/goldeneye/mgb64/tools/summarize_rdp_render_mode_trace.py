#!/usr/bin/env python3
"""Summarize native [RDP-MODE] render-mode audit rows."""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path


RDP_MODE_RE = re.compile(r"\[RDP-MODE\]\s+(?P<body>.*)")


def value_for(body: str, name: str) -> str | None:
    match = re.search(rf"\b{name}=([^\s]+)", body)
    return match.group(1) if match else None


def decode_value(body: str, name: str) -> str | None:
    match = re.search(rf"\b{name}=([^,\s}}]+)", body)
    return match.group(1) if match else None


def parse_row(body: str) -> dict[str, str]:
    decode_match = re.search(r"decode=\{(?P<decode>[^}]*)\}", body)
    decode = decode_match.group("decode") if decode_match else ""
    row = {
        "class": value_for(body, "class") or "?",
        "effect": value_for(body, "effect") or "-",
        "roommtx": value_for(body, "roommtx") or "0",
        "dl_room": value_for(body, "dl_room") or "-1",
        "dl": value_for(body, "dl") or "?",
        "raw": value_for(body, "raw") or "0x00000000",
        "eff": value_for(body, "eff") or "0x00000000",
        "omh": value_for(body, "omh") or "0x00000000",
        "geom": value_for(body, "geom") or "0x00000000",
        "cc": value_for(body, "cc") or "0x0000000000000000",
        "effcc": value_for(body, "effcc") or "0x0000000000000000",
        "opts": value_for(body, "opts") or "0x00000000",
        "settex": value_for(body, "settex") or "0",
        "texnum": value_for(body, "texnum") or "-1",
        "wh": value_for(body, "wh") or "0x0",
        "tex_used": value_for(body, "tex_used") or "(0,0)",
        "envA": value_for(body, "envA") or "?",
        "primA": value_for(body, "primA") or "?",
        "fogA": value_for(body, "fogA") or "?",
        "blend": value_for(body, "blend") or "?",
        "api_blend": value_for(body, "api_blend") or "?",
        "rdp_mem": value_for(body, "rdp_mem") or "none",
        "room_cvg": value_for(body, "room_cvg") or "0",
        "water_suppress": value_for(body, "water_suppress") or "0",
        "room_sort": value_for(body, "room_sort") or "0",
        "room_cvg_reason": value_for(body, "room_cvg_reason") or "?",
        "alpha": value_for(body, "alpha") or "0",
        "fog": value_for(body, "fog") or "0",
        "texedge": value_for(body, "texedge") or "0",
        "z": decode_value(decode, "z") or "?",
        "cvg": decode_value(decode, "cvg") or "?",
        "zcmp": decode_value(decode, "zcmp") or "0",
        "zupd": decode_value(decode, "zupd") or "0",
        "aa": decode_value(decode, "aa") or "0",
        "imrd": decode_value(decode, "imrd") or "0",
        "clr_on_cvg": decode_value(decode, "clr_on_cvg") or "0",
        "cvg_x_alpha": decode_value(decode, "cvg_x_alpha") or "0",
        "alpha_cvg": decode_value(decode, "alpha_cvg") or "0",
        "force_bl": decode_value(decode, "force_bl") or "0",
    }
    return row


def load_rows(paths: list[Path]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line_no, line in enumerate(handle, 1):
                match = RDP_MODE_RE.search(line)
                if not match:
                    continue
                row = parse_row(match.group("body"))
                row["source"] = f"{path}:{line_no}"
                rows.append(row)
    return rows


def signature(row: dict[str, str]) -> tuple[str, ...]:
    return (
        row["class"],
        row["effect"],
        f"roommtx={row['roommtx']}",
        f"dl_room={row['dl_room']}",
        f"dl={row['dl']}",
        row["raw"],
        row["cc"],
        row["opts"],
        row["blend"],
        row["api_blend"],
        row["rdp_mem"],
        f"cvg_reason={row['room_cvg_reason']}",
        row["settex"],
        row["texnum"],
        row["wh"],
        row["tex_used"],
        f"envA={row['envA']}",
        f"primA={row['primA']}",
        row["z"],
        row["cvg"],
        row["zcmp"],
        row["zupd"],
        row["imrd"],
        row["clr_on_cvg"],
        row["cvg_x_alpha"],
        row["alpha_cvg"],
        row["force_bl"],
        row["fog"],
        row["texedge"],
    )


def is_unpromoted_coverage_candidate(row: dict[str, str]) -> bool:
    return (
        row["blend"] == "alpha"
        and row["api_blend"] == "alpha"
        and row["z"] == "xlu"
        and row["cvg"] == "wrap"
        and row["imrd"] == "1"
        and row["clr_on_cvg"] == "1"
        and row["force_bl"] == "1"
        and row["cvg_x_alpha"] == "0"
        and row["alpha_cvg"] == "0"
    )


def bucket_payload(counter: collections.Counter[tuple[str, ...]], top: int) -> list[dict[str, object]]:
    return [
        {"count": count, "signature": list(sig)}
        for sig, count in counter.most_common(top)
    ]


def summarize(rows: list[dict[str, str]], top: int = 20) -> dict[str, object]:
    by_signature = collections.Counter(signature(row) for row in rows)
    by_raw_api = collections.Counter(
        (
            row["raw"],
            row["class"],
            f"roommtx={row['roommtx']}",
            f"dl_room={row['dl_room']}",
            f"dl={row['dl']}",
            row["cc"],
            row["opts"],
            row["blend"],
            row["api_blend"],
            row["rdp_mem"],
            f"cvg_reason={row['room_cvg_reason']}",
            f"z={row['z']}",
            f"cvg={row['cvg']}",
            f"fog={row['fog']}",
            f"texedge={row['texedge']}",
        )
        for row in rows
    )
    by_depth = collections.Counter(
        (
            row["z"],
            f"zcmp={row['zcmp']}",
            f"zupd={row['zupd']}",
            row["blend"],
            row["api_blend"],
        )
        for row in rows
    )
    candidates = [row for row in rows if is_unpromoted_coverage_candidate(row)]
    by_candidate = collections.Counter(signature(row) for row in candidates)
    by_candidate_gate = collections.Counter(
        (
            f"reason={row['room_cvg_reason']}",
            row["class"],
            f"roommtx={row['roommtx']}",
            f"dl_room={row['dl_room']}",
            f"dl={row['dl']}",
            row["raw"],
            row["cc"],
            row["opts"],
            f"settex={row['settex']}",
            f"texnum={row['texnum']}",
            f"wh={row['wh']}",
            f"tex_used={row['tex_used']}",
            f"envA={row['envA']}",
            f"primA={row['primA']}",
            f"fog={row['fog']}",
            f"texedge={row['texedge']}",
        )
        for row in candidates
    )

    return {
        "rows": len(rows),
        "unique_signatures": len(by_signature),
        "counts": {
            "api_blend": dict(sorted(collections.Counter(row["api_blend"] for row in rows).items())),
            "blend": dict(sorted(collections.Counter(row["blend"] for row in rows).items())),
            "dl": dict(sorted(collections.Counter(row["dl"] for row in rows).items())),
            "rdp_mem": dict(sorted(collections.Counter(row["rdp_mem"] for row in rows).items())),
            "raw": dict(sorted(collections.Counter(row["raw"] for row in rows).items())),
            "room_cvg": dict(sorted(collections.Counter(row["room_cvg"] for row in rows).items())),
            "room_cvg_reason": dict(sorted(collections.Counter(row["room_cvg_reason"] for row in rows).items())),
            "roommtx": dict(sorted(collections.Counter(row["roommtx"] for row in rows).items())),
            "z": dict(sorted(collections.Counter(row["z"] for row in rows).items())),
        },
        "promoted_coverage_memory_rows": sum(
            1 for row in rows
            if row["api_blend"] == "alpha_rdp_cvg_memory"
            and row["rdp_mem"] == "coverage"
        ),
        "unpromoted_coverage_candidate_rows": len(candidates),
        "top_raw_api": bucket_payload(by_raw_api, top),
        "top_depth": bucket_payload(by_depth, top),
        "top_unpromoted_coverage_candidates": bucket_payload(by_candidate, top),
        "top_unpromoted_coverage_candidate_gates": bucket_payload(by_candidate_gate, top),
    }


def print_bucket(title: str, items: list[dict[str, object]]) -> None:
    print(title)
    if not items:
        print("  none")
        return
    for item in items:
        sig = item["signature"]
        assert isinstance(sig, list)
        print(f"  {item['count']:5d}  " + " ".join(str(part) for part in sig))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    rows = load_rows(args.logs)
    if not rows:
        print("no RDP-MODE rows found", file=sys.stderr)
        return 1

    payload = summarize(rows, args.top)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                                 encoding="utf-8")

    print(f"rows: {payload['rows']}")
    print(f"unique signatures: {payload['unique_signatures']}")
    print_bucket("top raw/api buckets:", payload["top_raw_api"])  # type: ignore[arg-type]
    print_bucket("top depth buckets:", payload["top_depth"])  # type: ignore[arg-type]
    print_bucket(
        "unpromoted XLU coverage-wrap candidates:",
        payload["top_unpromoted_coverage_candidates"],  # type: ignore[arg-type]
    )
    print_bucket(
        "unpromoted XLU coverage-wrap candidate gates:",
        payload["top_unpromoted_coverage_candidate_gates"],  # type: ignore[arg-type]
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
