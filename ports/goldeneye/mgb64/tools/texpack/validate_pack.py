#!/usr/bin/env python3
"""validate_pack.py -- offline structural QA gate for an HD texture pack (W2.E7.T1).

The first, ROM-free leg of the pack QA harness (docs/design/remaster-aaa/02-hd-asset-pipeline.md
§4.8). Runs in seconds, touches no game binary, and catches the structural defects that
would otherwise only surface in-game (or, worse, ship):

  * filename shape        -- textures/ must contain only `tok####.png` (+ `_n`/`_r`
                             material sidecars); anything else is dead weight the loader
                             (texture_pack.c:75, `tok%04d.png`) silently ignores.
  * token range           -- the settex token is (w1 & 0xFFF) == 0..4095, so a 4-digit
                             token >= 4096 (TEXPACK_MAX_TOKENS, texture_pack.c:24) can
                             never be addressed.
  * decodable RGBA        -- stbi force-loads 4-channel at runtime (texture_pack.c:79);
                             a file that Pillow cannot decode will not decode there either.
  * dimension cap         -- BOTH backends reject width|height > 4096 (gfx_opengl.c:1531,
                             gfx_metal.mm:1922) -> the upload is dropped, texture renders
                             stock. A hard FAIL.
  * alpha parity          -- the manifest CSV has NO alpha column (schema frozen to the 8
                             roadmap-P1.2 columns), so alpha truth comes from the dump's
                             `.alpha.pgm` sidecars via --dump. A cutout (grate/glass/wheel)
                             that lost its transparency through the upscaler is a dropped
                             cutout -> FAIL.
  * seam self-check       -- for tokens the manifest marks tileable, the produced HD tile
                             must STILL wrap (the shared is_tileable edge metric). A tile
                             that no longer wraps step-repeats across a large surface -> FAIL.
  * upload budget         -- sum of decoded RGBA bytes vs a per-level budget (default
                             256 MB; a 4096^2 RGBA texture is 64 MB, so this also catches
                             "everything upscaled to 4096" packs). Prints the top-10
                             offenders.

R2 / copyright: this tool reads ONLY the token/w/h/tileable/draw_class manifest columns
(never avgRGB, the one ROM-derived column) and, with --dump, the LOCAL, never-committed
`.alpha.pgm` sidecars. It emits no ROM-derived data. Its pytest builds every fixture
in-test; no image is tracked.

Usage:
    validate_pack.py --pack <dir> --manifest <csv> [--dump <dir>] [--budget-mb 256]

Exit 0 + `VALIDATE_PACK PASS ...` when clean; exit 1 with one
`VALIDATE_PACK FAIL check=<name> ...` line per defect otherwise.
"""
import argparse
import glob
import os
import re
import sys
from collections import namedtuple

# The is_tileable edge metric and the token-key canonicalizer are the SHARED
# implementations from the sibling build_pack.py (ONE copy, no drift -- the same
# metric the C manifest emit and route_pack.py agree with, W2.E1.T2). The manifest
# reader is route_pack.py's (header-driven, tolerant of column reorder). Both
# sibling tools run as __main__ from this directory, so make them importable
# regardless of how THIS file was loaded (script or pytest).
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from build_pack import is_tileable, parse_token  # noqa: E402
from route_pack import load_manifest  # noqa: E402

# --- shape / limits (all anchored to the runtime, see module docstring) ---------
PRIMARY_RE = re.compile(r"^tok(\d{4})\.png$")          # the diffuse texture
SIDECAR_RE = re.compile(r"^tok(\d{4})_([nr])\.png$")   # _n normal / _r roughness
MAX_TOKENS = 4096          # texture_pack.c:24  (token is 0..4095)
MAX_DIM = 4096             # gfx_opengl.c:1531 / gfx_metal.mm:1922 (both backends)
DEFAULT_BUDGET_MB = 256    # §4.8 per-level default
BYTES_PER_TEXEL = 4        # forced RGBA8 upload (texture_pack.c:79)

# alpha "presence": a channel has meaningful transparency if at least this fraction
# of texels are notably non-opaque. The near-opaque tail (>= 250) is treated as
# opaque so upscaler edge anti-aliasing does not read as a cutout.
ALPHA_OPAQUE_CUTOFF = 250
ALPHA_PRESENCE_FRAC = 0.01

ASPECT_TOL = 0.01          # relative aspect-ratio drift tolerated before a WARN


Finding = namedtuple("Finding", ["level", "check", "token", "msg"])


class Report:
    """Structured outcome. `ok` iff no FAIL-level findings."""

    def __init__(self):
        self.findings = []          # all Findings (FAIL + WARN), in emission order
        self.total_bytes = 0
        self.offenders = []         # [(bytes, name)] for the budget top-10
        self.texture_count = 0

    def fail(self, check, token, msg):
        self.findings.append(Finding("FAIL", check, token, msg))

    def warn(self, check, token, msg):
        self.findings.append(Finding("WARN", check, token, msg))

    @property
    def failures(self):
        return [f for f in self.findings if f.level == "FAIL"]

    @property
    def warnings(self):
        return [f for f in self.findings if f.level == "WARN"]

    @property
    def ok(self):
        return not self.failures

    def has_fail(self, check):
        """True if any FAIL with this check name was recorded (test helper)."""
        return any(f.check == check for f in self.failures)


def _pil():
    try:
        from PIL import Image
        return Image
    except ImportError:
        sys.exit("Pillow required: pip install -r tools/texpack/requirements.txt")


def _is_pow2(n):
    return n > 0 and (n & (n - 1)) == 0


def _alpha_present(single_channel_img):
    """True if the L/A channel carries meaningful transparency. Uses the 256-bin
    histogram (size-independent, fast even on 4096^2 images)."""
    hist = single_channel_img.histogram()          # 256 bins for an 8-bit channel
    total = sum(hist)
    if total == 0:
        return False
    non_opaque = sum(hist[:ALPHA_OPAQUE_CUTOFF])
    return (non_opaque / total) >= ALPHA_PRESENCE_FRAC


def _dump_alpha_index(dump_dir):
    """Map {token_key: alpha_pgm_path} for every `.alpha.pgm` in the dump. The engine
    writes `<base>.alpha.pgm` next to `<base>.rgba.ppm` for each settex token
    (gfx_pc.c:12306); parse_token recovers the runtime `tok####` key from the base."""
    index = {}
    for p in sorted(glob.glob(os.path.join(dump_dir, "*.alpha.pgm"))):
        tok = parse_token(os.path.basename(p))
        if tok is not None:
            index.setdefault(tok, p)
    return index


def validate_pack(pack_dir, manifest_path, dump_dir=None, budget_mb=DEFAULT_BUDGET_MB):
    """Run every structural check and return a Report. Never raises on a bad pack --
    defects are recorded as FAIL/WARN findings; only genuinely missing inputs
    (no manifest, no textures/ dir) are structural FAILs."""
    Image = _pil()
    rep = Report()

    manifest = load_manifest(manifest_path) if manifest_path else {}
    alpha_index = _dump_alpha_index(dump_dir) if dump_dir else None

    tex_dir = os.path.join(pack_dir, "textures")
    if not os.path.isdir(tex_dir):
        rep.fail("no-textures-dir", pack_dir,
                 f"pack has no readable 'textures/' directory (looked for {tex_dir})")
        return rep

    pngs = sorted(glob.glob(os.path.join(tex_dir, "*.png")))
    if not pngs:
        rep.fail("empty-pack", pack_dir, f"no *.png textures under {tex_dir}")
        return rep

    for path in pngs:
        name = os.path.basename(path)
        mp = PRIMARY_RE.match(name)
        ms = SIDECAR_RE.match(name)
        if mp:
            token_num = int(mp.group(1))
            is_sidecar = False
        elif ms:
            token_num = int(ms.group(1))
            is_sidecar = True
        else:
            # tok22.png (too few digits), tok00042.png (too many), foo.png, etc.
            rep.fail("bad-filename", name,
                     "does not match tok####.png (or tok####_n/_r.png sidecar)")
            continue

        token = "tok%04d" % token_num
        if token_num >= MAX_TOKENS:
            rep.fail("token-range", token,
                     f"token {token_num} >= {MAX_TOKENS} (settex token is 0..4095)")
            continue

        try:
            im = Image.open(path)
            im = im.convert("RGBA")
        except Exception as exc:  # noqa: BLE001  (any decode failure is a FAIL)
            rep.fail("decode", token, f"not a decodable image ({exc})")
            continue

        w, h = im.size

        # decoded RGBA upload cost -> budget (primaries AND sidecars occupy VRAM)
        nbytes = w * h * BYTES_PER_TEXEL
        rep.total_bytes += nbytes
        rep.offenders.append((nbytes, name))
        rep.texture_count += 1

        # dimension cap -- BOTH backends drop the upload above 4096 (hard FAIL)
        if w > MAX_DIM or h > MAX_DIM:
            rep.fail("dims-oversize", token,
                     f"{w}x{h} exceeds the {MAX_DIM} cap (upload rejected on both "
                     f"backends -> renders stock)")
            # keep going: still worth reporting other defects on this file

        if not (_is_pow2(w) and _is_pow2(h)):
            rep.warn("npot", token, f"{w}x{h} is not power-of-two")

        # -------- diffuse-only checks (sidecars are material maps) ---------------
        if is_sidecar:
            continue

        row = manifest.get(token)

        # aspect vs manifest (HD should be an integer upscale -> aspect preserved)
        if row is not None and row.w > 0 and row.h > 0 and h > 0:
            man_aspect = row.w / row.h
            hd_aspect = w / h
            if man_aspect > 0 and abs(hd_aspect - man_aspect) / man_aspect > ASPECT_TOL:
                rep.warn("aspect", token,
                         f"HD aspect {w}x{h} ({hd_aspect:.3f}) drifts from manifest "
                         f"{row.w}x{row.h} ({man_aspect:.3f})")

        # alpha parity vs the dump's original alpha (needs --dump)
        if alpha_index is not None:
            apath = alpha_index.get(token)
            if apath is not None:
                try:
                    orig_present = _alpha_present(Image.open(apath).convert("L"))
                    hd_present = _alpha_present(im.getchannel("A"))
                except Exception as exc:  # noqa: BLE001
                    rep.warn("alpha-read", token, f"could not compare alpha ({exc})")
                else:
                    if orig_present and not hd_present:
                        rep.fail("alpha-dropped", token,
                                 "original has an alpha cutout but the HD texture is "
                                 "opaque (dropped transparency)")
                    elif hd_present and not orig_present:
                        rep.warn("alpha-added", token,
                                 "HD texture has transparency the original lacked "
                                 "(spurious alpha)")

        # seam self-check -- a manifest-tileable token must STILL wrap
        if row is not None and row.tileable:
            if not is_tileable(im):
                rep.fail("seam-broken", token,
                         "manifest marks this token tileable but the HD output no "
                         "longer wraps (seam would step-repeat across the surface)")

    # -------- upload budget --------------------------------------------------
    rep.offenders.sort(reverse=True)
    budget_bytes = budget_mb * 1024 * 1024
    if rep.total_bytes > budget_bytes:
        rep.fail("budget", pack_dir,
                 f"decoded RGBA total {rep.total_bytes / (1024*1024):.1f} MB exceeds "
                 f"the {budget_mb} MB budget")

    return rep


def _print_report(rep, pack_dir, budget_mb, dump_dir):
    """Human-readable + machine-greppable rendering of a Report to stdout."""
    for f in rep.findings:
        tag = "VALIDATE_PACK FAIL" if f.level == "FAIL" else "VALIDATE_PACK WARN"
        print(f"{tag} check={f.check} token={f.token}: {f.msg}")

    # budget top-10 offenders (always informative)
    if rep.offenders:
        print(f"VALIDATE_PACK budget: {rep.total_bytes / (1024*1024):.1f} MB "
              f"decoded RGBA across {rep.texture_count} textures "
              f"(budget {budget_mb} MB) -- top offenders:")
        for nbytes, name in rep.offenders[:10]:
            print(f"    {nbytes / (1024*1024):7.2f} MB  {name}")

    if dump_dir is None:
        print("VALIDATE_PACK note: --dump not given; alpha-parity check skipped "
              "(manifest CSV has no alpha column).")

    if rep.ok:
        print(f"VALIDATE_PACK PASS pack={pack_dir} textures={rep.texture_count} "
              f"warnings={len(rep.warnings)} "
              f"mb={rep.total_bytes / (1024*1024):.1f}")
    else:
        print(f"VALIDATE_PACK FAIL pack={pack_dir} "
              f"fails={len(rep.failures)} warnings={len(rep.warnings)}")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Offline structural QA gate for an HD texture pack (W2.E7.T1).")
    ap.add_argument("--pack", required=True, help="pack directory (contains textures/)")
    ap.add_argument("--manifest", required=True,
                    help="W2.E1 texmanifest CSV (token,w,h,fmt,siz,avgRGB,tileable,draw_class)")
    ap.add_argument("--dump", default=None,
                    help="dump directory with .alpha.pgm sidecars (enables alpha-parity)")
    ap.add_argument("--budget-mb", type=int, default=DEFAULT_BUDGET_MB,
                    help=f"per-level upload budget in MB (default {DEFAULT_BUDGET_MB})")
    args = ap.parse_args(argv)

    if not os.path.isdir(args.pack):
        sys.exit(f"--pack '{args.pack}' is not a directory")
    if not os.path.isfile(args.manifest):
        sys.exit(f"--manifest '{args.manifest}' is not a file")
    if args.dump is not None and not os.path.isdir(args.dump):
        sys.exit(f"--dump '{args.dump}' is not a directory")

    rep = validate_pack(args.pack, args.manifest, args.dump, args.budget_mb)
    _print_report(rep, args.pack, args.budget_mb, args.dump)
    return 0 if rep.ok else 1


if __name__ == "__main__":
    sys.exit(main())
