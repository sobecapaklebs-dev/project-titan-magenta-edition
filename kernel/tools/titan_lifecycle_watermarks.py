#!/usr/bin/env python3
"""Compare 2..N Titan debug reports for monotonic memory/residency growth.

This is a host-side diagnostic tool. It never runs in the render loop.
A FAIL means every observed step increased and the final value did not plateau;
it does not label bounded cache warm-up as a leak when values stabilize.
"""
from __future__ import annotations
import argparse, re, sys
from pathlib import Path

PATTERNS = {
    "vram_used": re.compile(r"VRAM used/free:\s*(\d+)\s*/\s*(\d+)"),
    "ee_used": re.compile(r"EE RAM estimate: valid=(\d+) used=(\d+) free=(\d+)"),
    "iop_used": re.compile(r"IOP RAM: valid=(\d+) used=(\d+) free=(\d+)"),
    "textures": re.compile(r"Texture residency:\s*(\d+)\s*\((\d+) bytes\)"),
    "theme_frames": re.compile(r"Theme resident frames:\s*(\d+)"),
}

def parse(path: Path):
    text=path.read_text(errors="replace")
    out={}
    m=PATTERNS["vram_used"].search(text)
    if m: out["vram_used"]=int(m.group(1))
    m=PATTERNS["ee_used"].search(text)
    if m and int(m.group(1)): out["ee_used"]=int(m.group(2))
    m=PATTERNS["iop_used"].search(text)
    if m and int(m.group(1)): out["iop_used"]=int(m.group(2))
    m=PATTERNS["textures"].search(text)
    if m: out["textures"]=int(m.group(1)); out["texture_bytes"]=int(m.group(2))
    m=PATTERNS["theme_frames"].search(text)
    if m: out["theme_frames"]=int(m.group(1))
    return out

def monotonic_unbounded(values):
    if len(values)<3: return False
    return all(b>a for a,b in zip(values,values[1:]))

def main(argv=None):
    ap=argparse.ArgumentParser()
    ap.add_argument("reports", nargs="+", type=Path)
    ns=ap.parse_args(argv)
    if len(ns.reports)<2:
        ap.error("at least two reports are required")
    rows=[parse(p) for p in ns.reports]
    keys=sorted(set().union(*[r.keys() for r in rows]))
    failed=[]
    for k in keys:
        vals=[r[k] for r in rows if k in r]
        if len(vals)!=len(rows):
            print(f"{k}: NOT_MEASURED in every cycle")
            continue
        status="FAIL_MONOTONIC_GROWTH" if monotonic_unbounded(vals) else "OK_BOUNDED_OR_NONMONOTONIC"
        print(f"{k}: {vals} {status}")
        if status.startswith("FAIL"): failed.append(k)
    return 1 if failed else 0
if __name__=="__main__": raise SystemExit(main())
