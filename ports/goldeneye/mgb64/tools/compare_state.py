#!/usr/bin/env python3
"""Compare two JSONL state traces and report first divergence.

Usage: compare_state.py baseline.jsonl test.jsonl [--tolerance 0.1]

Exit code 0 = match, 1 = divergence found, 2 = error.
"""
import json, os, re, sys

FLOAT_TOLERANCE = 0.1  # default tolerance for float fields
GEOM_CULL_BOTH_MASK = 0x00003000
TRI_TOLERANCE_BY_LEVEL = {
    "43": 3,   # Surface 2 room-visibility jitter
    "37": 32,  # Jungle foliage/prop cull-count drift with stable pixels
    "41": 8,   # Cradle distant prop/room count drift with stable pixels
    "28": 1,   # Aztec one-frame count blip with stable pixels
}
EXACT_FIELDS = {"f", "p", "crashes", "bad_cmds", "nan",
                 "fog", "fog_mul", "fog_off", "segs",
                 "cam", "cam_after", "icam", "p_unk"}  # must match exactly

def tri_tolerance_for_paths(*paths):
    """Keep tri tolerance exact by default and scope exceptions tightly.

    A few late-game stages have cull/visibility-sensitive draw count drift
    while the pixel lane remains stable. Keep those exceptions level-local.
    """
    for path in paths:
        match = re.fullmatch(r"trace_(\d+)\.jsonl", os.path.basename(path))
        if match and match.group(1) in TRI_TOLERANCE_BY_LEVEL:
            return TRI_TOLERANCE_BY_LEVEL[match.group(1)]
    return 0

def load_trace(path):
    frames = []
    skipped = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                frames.append(json.loads(line))
            except json.JSONDecodeError:
                # Lines can be corrupted by siglongjmp during DL crash recovery
                skipped += 1
    if skipped > 0:
        print(f"WARNING: skipped {skipped} corrupted trace line(s) in {path}", file=sys.stderr)
    return frames

def normalized_geom(value):
    """Normalize volatile final-frame geometry state.

    The trace samples renderer state after the frame, so the final HUD, sky, or
    effect draw can leave G_CULL_FRONT/G_CULL_BACK toggled without changing the
    game state or captured pixels. Keep every other geometry bit exact.
    """
    if isinstance(value, str):
        try:
            value = int(value, 16)
        except ValueError:
            return value
    if isinstance(value, int):
        return value & ~GEOM_CULL_BOTH_MASK
    return value

def compare_values(key, a, b, tol, tri_tol, path=""):
    """Returns None if match, or a description of the difference."""
    if key == "geom":
        if normalized_geom(a) != normalized_geom(b):
            return f"{a} -> {b}"
        return None
    if key == "tris":
        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
            if abs(a - b) > tri_tol:
                return f"{a} -> {b}"
            return None
    if key in EXACT_FIELDS:
        if a != b:
            return f"{a} -> {b}"
        return None

    if isinstance(a, dict) and isinstance(b, dict):
        diffs = []
        for subkey in sorted(a.keys()):
            subpath = f"{path}.{subkey}" if path else subkey
            if subkey not in b:
                diffs.append(f"{subpath}: MISSING in test")
                continue
            diff = compare_values(subkey, a[subkey], b[subkey], tol, tri_tol, subpath)
            if diff:
                diffs.append(diff)
        return "; ".join(diffs) if diffs else None

    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return f"len {len(a)} -> {len(b)}"
        diffs = []
        for i, (va, vb) in enumerate(zip(a, b)):
            item_path = f"{path}[{i}]" if path else f"[{i}]"
            if isinstance(va, (int, float)) and isinstance(vb, (int, float)):
                if abs(va - vb) > tol:
                    diffs.append(f"{item_path}: {va:.4f} -> {vb:.4f}")
            elif isinstance(va, (dict, list)) and isinstance(vb, (dict, list)):
                diff = compare_values(key, va, vb, tol, tri_tol, item_path)
                if diff:
                    diffs.append(diff)
            elif va != vb:
                diffs.append(f"{item_path}: {va} -> {vb}")
        return ", ".join(diffs) if diffs else None

    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        if abs(a - b) > tol:
            return f"{a:.4f} -> {b:.4f}"
        return None

    if a != b:
        return f"{a} -> {b}"
    return None

def main():
    tol = FLOAT_TOLERANCE
    args = sys.argv[1:]
    if "--tolerance" in args:
        idx = args.index("--tolerance")
        tol = float(args[idx + 1])
        args = args[:idx] + args[idx+2:]

    if len(args) != 2:
        print(f"Usage: {sys.argv[0]} baseline.jsonl test.jsonl [--tolerance N]")
        sys.exit(2)

    baseline_path, test_path = args
    tri_tol = tri_tolerance_for_paths(baseline_path, test_path)
    baseline = load_trace(baseline_path)
    test = load_trace(test_path)

    min_len = min(len(baseline), len(test))
    if len(baseline) != len(test):
        print(f"FAIL: frame count differs: {len(baseline)} vs {len(test)}")
        sys.exit(1)

    divergent_count = 0
    for i in range(min_len):
        bf, tf = baseline[i], test[i]
        frame_diffs = {}
        all_keys = set(bf.keys()) | set(tf.keys())
        for key in sorted(all_keys):
            if key not in bf:
                continue
            if key not in tf:
                frame_diffs[key] = f"MISSING in test"
                continue
            diff = compare_values(key, bf[key], tf[key], tol, tri_tol, key)
            if diff:
                frame_diffs[key] = diff

        if frame_diffs:
            divergent_count += 1
            if divergent_count <= 3:
                print(f"DIVERGENCE at frame {bf.get('f', i+1)}:")
                for key, desc in frame_diffs.items():
                    print(f"  {key}: {desc}")
            if divergent_count == 3:
                remaining = min_len - i - 1
                if remaining > 0:
                    print(f"  ... ({remaining} more frames not shown)")
                break

    if divergent_count == 0:
        print(f"MATCH: {min_len} frames identical (tolerance={tol}, tri_tolerance={tri_tol})")
        sys.exit(0)
    else:
        print(f"TOTAL: {divergent_count} divergent frame(s) in first {i+1} checked")
        sys.exit(1)

if __name__ == "__main__":
    main()
