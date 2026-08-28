#!/usr/bin/env python3
"""hue_pack.py -- W2.E8.T1: hero-token identification via the "hue technique".

Mechanizes the validated manual Dam technique (roadmap §3 "Dam learnings"): N64
vertex shading is a *grayscale multiply*, so a texture's hue survives to screen
untouched (only its brightness is modulated). So if we replace every ROOM token
with a flat, maximally-saturated, uniquely-hued tile, the on-screen hue of each
surface tells us exactly which token painted it -- and the pixel coverage of each
hue ranks the tokens by how much of the frame they own. The top ~5 ROOM tokens by
coverage are the hero-surface candidates a curator should hand-route
(docs/design/remaster-aaa/02-hd-asset-pipeline.md §4.7).

Two modes:

  emit (default):
      hue_pack.py --manifest <texmanifest.csv> --out <dir>
    Paints every ROOM token (draw_class == "room") a unique HSV hue, writes flat
    1-colour <dir>/textures/tok####.png tiles (the loader probes exactly
    <pack>/textures/tok%04d.png, texture_pack.c:51-58,75) plus <dir>/hue_map.json
    (the token->hue-angle map + the identify thresholds). Prints the map.

  identify:
      hue_pack.py --identify <screenshot.bmp> --map <hue_map.json>
    Clusters the screenshot's pixels by hue angle (tolerant of luma variation --
    the grayscale-multiply darkening -- because it keys on hue, not brightness),
    ranks the on-screen ROOM tokens by pixel coverage, and prints the ranked list.
    The #1 line ("TOP ROOM TOKEN: tok####") is the dominant ground surface.

Dependency-free: stdlib only. PNG tiles are written with `zlib`; the .bmp
screenshot (the engine's Tier-B format, platform_sdl.c:701-727) is decoded by
hand. A .png passed to --identify falls back to Pillow if present.

R2 (copyright): everything this tool *produces* -- the flat pack, the hue_map,
the screenshots it is fed -- is ROM-derived throwaway and is .gitignored. Only
this script is committed.
"""

import argparse
import colorsys
import csv
import json
import os
import re
import struct
import sys
import zlib

# --------------------------------------------------------------------------- #
# Token canonicalisation. Mirrors build_pack.normalize_token_key so the runtime
# pack key (`tok####`) is identical to what route_pack/build_pack emit; kept
# inline (not imported) so this tool stays standalone / dependency-free. The
# engine already writes the `token` column as `tok%04d` (gfx_pc.c:12579), so this
# is the identity map in practice -- the regex just tolerates hand-edited input.
_TOKEN_RE = re.compile(r"(?:settex_|tok)?(\d+)\s*$")


def normalize_token_key(raw):
    """Return the canonical `tok####` key for a token string, or None."""
    if raw is None:
        return None
    m = _TOKEN_RE.search(str(raw).strip())
    if not m:
        return None
    return "tok%04d" % int(m.group(1))


# ------------------------------------------------------------------- manifest #

def load_room_tokens(path):
    """Parse a W2.E1 texmanifest CSV (token,w,h,fmt,siz,avgRGB,tileable,draw_class)
    and return the sorted, de-duplicated list of ROOM tokens (draw_class=="room").

    Header-driven and tolerant of column reordering, matching route_pack.py's
    reader. Only the `token` and `draw_class` columns are consulted; avgRGB (the
    one ROM-derived column) is deliberately never read."""
    rooms = []
    seen = set()
    with open(path, newline="") as fp:
        reader = csv.reader(fp)
        header = None
        for parts in reader:
            parts = [p.strip() for p in parts]
            if not parts or not parts[0]:
                continue
            if header is None and any(p.lower() == "token" for p in parts):
                header = [p.lower() for p in parts]
                continue
            if header is None:
                continue

            def col(name, default=""):
                if name in header:
                    i = header.index(name)
                    if i < len(parts):
                        return parts[i]
                return default

            tok = normalize_token_key(col("token", parts[0]))
            if tok is None:
                continue
            if col("draw_class", "").lower() != "room":
                continue
            if tok in seen:
                continue
            seen.add(tok)
            rooms.append(tok)
    rooms.sort()
    return rooms


# ----------------------------------------------------------------------- hue #

def assign_hues(tokens):
    """Assign each token a maximally-spaced hue on the colour wheel.

    For a fixed count N, even spacing (i*360/N) is the maximally-spaced
    assignment. `tokens` must already be in a deterministic order (the caller
    sorts), so the map is byte-stable across runs."""
    n = len(tokens)
    if n == 0:
        return {}
    return {tok: (i * 360.0 / n) for i, tok in enumerate(tokens)}


def default_hue_tolerance(n):
    """Half the inter-hue spacing (capped) -- the max angular error a pixel may
    have from its token's pure hue and still be attributed to it."""
    if n <= 0:
        return 25.0
    half = 180.0 / n
    return max(3.0, min(half * 0.95, 25.0))


def hue_to_rgb255(hue_deg, sat=1.0, val=1.0):
    """Pure (max-saturation, max-value) colour for a hue angle -> 0..255 RGB.
    Grayscale vertex-shade later multiplies all three channels by the same k, so
    the on-screen hue == this hue exactly (only value changes)."""
    r, g, b = colorsys.hsv_to_rgb((hue_deg % 360.0) / 360.0, sat, val)
    return (int(round(r * 255)), int(round(g * 255)), int(round(b * 255)))


# ----------------------------------------------------------------- PNG write #

def _png_chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_flat_png(path, rgb, size=16):
    """Write a flat `size`x`size` RGBA8 PNG of colour `rgb` (stdlib only).
    stb_image (the loader) force-decodes to 4 channels, so colour-type 6 with
    alpha=255 loads identically to the stock decoded master."""
    r, g, b = rgb
    row = b"\x00" + bytes([r, g, b, 255]) * size   # filter byte 0 + RGBA pixels
    raw = row * size
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8bpc, RGBA
    png = (b"\x89PNG\r\n\x1a\n"
           + _png_chunk(b"IHDR", ihdr)
           + _png_chunk(b"IDAT", zlib.compress(raw, 9))
           + _png_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


# ------------------------------------------------------------------- BMP read #

def read_bmp(path):
    """Decode an uncompressed 24/32-bit BMP (the engine's screenshot format,
    platform_sdl.c:701-727: bottom-up, BGR, 4-byte-aligned rows) using stdlib
    only. Returns (width, height, pixels) with pixels a flat list of (r,g,b),
    top-to-bottom row order."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        raise ValueError("%s: not a BMP (missing 'BM' magic)" % path)
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if compression != 0:
        raise ValueError("%s: compressed BMP (compression=%d) unsupported"
                         % (path, compression))
    if bpp not in (24, 32):
        raise ValueError("%s: %d-bpp BMP unsupported (need 24 or 32)"
                         % (path, bpp))
    top_down = height < 0
    h = abs(height)
    bpx = bpp // 8
    row_size = ((bpp * width + 31) // 32) * 4    # rows padded to 4 bytes
    pixels = []
    for y in range(h):
        srcy = y if top_down else (h - 1 - y)
        base = pixel_offset + srcy * row_size
        for x in range(width):
            px = base + x * bpx
            b = data[px]
            g = data[px + 1]
            r = data[px + 2]
            pixels.append((r, g, b))
    return width, h, pixels


def load_image_rgb(path):
    """(width, height, [(r,g,b),...]) for a screenshot. .bmp is decoded with
    stdlib; anything else falls back to Pillow if it is installed."""
    if os.path.splitext(path)[1].lower() == ".bmp":
        return read_bmp(path)
    try:
        from PIL import Image
    except ImportError:
        sys.exit("hue_pack --identify: %s needs Pillow to decode "
                 "(only .bmp is dependency-free)." % path)
    im = Image.open(path).convert("RGB")
    w, h = im.size
    return w, h, list(im.getdata())


# --------------------------------------------------------------------- emit #

def emit_pack(manifest_path, out_dir, size=16):
    """Emit the hue pack + hue_map.json. Returns the token->hue dict."""
    tokens = load_room_tokens(manifest_path)
    if not tokens:
        sys.exit("hue_pack emit: no ROOM tokens (draw_class==room) in %s -- "
                 "nothing to paint. Check the manifest / the dump ran on-level."
                 % manifest_path)
    hues = assign_hues(tokens)
    tex_dir = os.path.join(out_dir, "textures")
    os.makedirs(tex_dir, exist_ok=True)
    for tok in tokens:
        write_flat_png(os.path.join(tex_dir, tok + ".png"),
                       hue_to_rgb255(hues[tok]), size=size)
    hue_map = {
        "version": 1,
        "count": len(tokens),
        "saturation_min": 0.5,
        "value_min": 0.06,
        "value_max": 1.0,
        "hue_tolerance_deg": round(default_hue_tolerance(len(tokens)), 4),
        "tokens": {tok: round(hues[tok], 4) for tok in tokens},
    }
    with open(os.path.join(out_dir, "hue_map.json"), "w") as f:
        json.dump(hue_map, f, indent=2, sort_keys=True)
        f.write("\n")
    return hues


def cmd_emit(args):
    hues = emit_pack(args.manifest, args.out, size=args.size)
    print("[hue_pack] emitted %d ROOM tokens -> %s"
          % (len(hues), os.path.join(args.out, "textures")))
    print("[hue_pack] hue map -> %s" % os.path.join(args.out, "hue_map.json"))
    for tok in sorted(hues):
        print("    %-9s hue=%7.2f deg  rgb=%s"
              % (tok, hues[tok], hue_to_rgb255(hues[tok])))
    return 0


# ----------------------------------------------------------------- identify #

def _circular_hue_dist(a, b):
    d = abs(a - b) % 360.0
    return min(d, 360.0 - d)


def identify(width, height, pixels, tokens_hue,
             sat_min, val_min, val_max, hue_tol):
    """Cluster `pixels` (list of (r,g,b)) by hue angle and attribute each
    qualifying pixel to the nearest token hue within `hue_tol`. Returns
    (ranked, classified, total) where ranked is [(token, count, coverage_pct)]
    sorted by count desc (token id tiebreak)."""
    tok_list = sorted(tokens_hue.items())   # deterministic order
    counts = {tok: 0 for tok in tokens_hue}
    total = len(pixels)
    classified = 0
    for (r, g, b) in pixels:
        hh, ss, vv = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
        if ss < sat_min or vv < val_min or vv > val_max:
            continue           # grayscale / near-black / blown-out: hue unstable
        hue = hh * 360.0
        best = None
        best_d = None
        for tok, thue in tok_list:
            d = _circular_hue_dist(hue, thue)
            if best_d is None or d < best_d:
                best_d = d
                best = tok
        if best is not None and best_d <= hue_tol:
            counts[best] += 1
            classified += 1
    denom = float(total) if total else 1.0
    ranked = sorted(((tok, c, 100.0 * c / denom) for tok, c in counts.items()),
                    key=lambda kv: (-kv[1], kv[0]))
    return ranked, classified, total


def load_hue_map(path):
    """Load hue_map.json -> (tokens_hue dict, thresholds dict)."""
    with open(path) as f:
        data = json.load(f)
    tokens = {normalize_token_key(k): float(v)
              for k, v in data.get("tokens", {}).items()
              if normalize_token_key(k) is not None}
    thresholds = {
        "saturation_min": float(data.get("saturation_min", 0.5)),
        "value_min": float(data.get("value_min", 0.06)),
        "value_max": float(data.get("value_max", 1.0)),
        "hue_tolerance_deg": float(data.get("hue_tolerance_deg",
                                            default_hue_tolerance(len(tokens)))),
    }
    return tokens, thresholds


def cmd_identify(args):
    tokens_hue, th = load_hue_map(args.map)
    if not tokens_hue:
        sys.exit("hue_pack --identify: %s has no tokens." % args.map)
    sat_min = args.sat_min if args.sat_min is not None else th["saturation_min"]
    val_min = args.val_min if args.val_min is not None else th["value_min"]
    val_max = args.val_max if args.val_max is not None else th["value_max"]
    hue_tol = args.hue_tol if args.hue_tol is not None else th["hue_tolerance_deg"]

    width, height, pixels = load_image_rgb(args.identify)
    ranked, classified, total = identify(width, height, pixels, tokens_hue,
                                         sat_min, val_min, val_max, hue_tol)

    cov = (100.0 * classified / total) if total else 0.0
    print("[hue_pack] identify %s  (%dx%d, %d/%d px classified = %.1f%%, "
          "sat>=%.2f val=[%.2f,%.2f] hue_tol=%.1f deg)"
          % (args.identify, width, height, classified, total, cov,
             sat_min, val_min, val_max, hue_tol))
    if classified == 0:
        print("[hue_pack] WARNING: no pixels matched any token hue -- is this "
              "the hue-pack screenshot (GE007_TEXTURE_PACK=<pack>)?")
    print("%4s  %-9s  %10s  %9s" % ("rank", "token", "pixels", "coverage"))
    nonzero = [row for row in ranked if row[1] > 0]
    shown = nonzero[:args.top] if nonzero else ranked[:args.top]
    for i, (tok, count, pct) in enumerate(shown, 1):
        print("%4d  %-9s  %10d  %8.2f%%" % (i, tok, count, pct))
    top = nonzero[0][0] if nonzero else (ranked[0][0] if ranked else "(none)")
    print("TOP ROOM TOKEN: %s" % top)

    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump({"screenshot": os.path.basename(args.identify),
                       "width": width, "height": height,
                       "classified": classified, "total": total,
                       "top": top,
                       "ranked": [{"token": t, "pixels": c,
                                   "coverage_pct": round(p, 4)}
                                  for t, c, p in nonzero]},
                      f, indent=2, sort_keys=True)
            f.write("\n")
    return 0


# --------------------------------------------------------------------- main #

def build_parser():
    p = argparse.ArgumentParser(
        description="Hue-technique hero-token identification (W2.E8.T1).")
    p.add_argument("--identify", metavar="SCREENSHOT",
                   help="identify mode: rank on-screen ROOM tokens in this "
                        ".bmp/.png screenshot (requires --map).")
    p.add_argument("--map", metavar="HUE_MAP_JSON",
                   help="identify mode: the hue_map.json emitted by emit mode.")
    p.add_argument("--manifest", metavar="TEXMANIFEST_CSV",
                   help="emit mode: the W2.E1 texmanifest CSV to read ROOM "
                        "tokens from.")
    p.add_argument("--out", metavar="DIR",
                   help="emit mode: pack output dir (writes textures/ + "
                        "hue_map.json).")
    p.add_argument("--size", type=int, default=16,
                   help="emit mode: flat-tile edge in px (default 16).")
    p.add_argument("--sat-min", type=float, default=None,
                   help="identify: min saturation for a pixel to count "
                        "(default from hue_map / 0.5).")
    p.add_argument("--val-min", type=float, default=None,
                   help="identify: min value (default from hue_map / 0.06).")
    p.add_argument("--val-max", type=float, default=None,
                   help="identify: max value (default from hue_map / 1.0).")
    p.add_argument("--hue-tol", type=float, default=None,
                   help="identify: max hue-angle error in deg "
                        "(default from hue_map / half spacing).")
    p.add_argument("--top", type=int, default=15,
                   help="identify: how many ranked rows to print (default 15).")
    p.add_argument("--json-out", metavar="JSON",
                   help="identify: also write the ranking to this JSON file.")
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.identify is not None:
        if not args.map:
            build_parser().error("--identify requires --map <hue_map.json>")
        return cmd_identify(args)
    # emit (default)
    if not args.manifest or not args.out:
        build_parser().error(
            "emit mode requires --manifest <csv> and --out <dir> "
            "(or use --identify <screenshot> --map <hue_map.json>)")
    return cmd_emit(args)


if __name__ == "__main__":
    sys.exit(main())
