#!/usr/bin/env python3
"""
build_pack.py -- turn a MGB64 texture dump into an HD texture pack via Real-ESRGAN.

Pipeline (all local, all from YOUR ROM dump -- nothing here is redistributable):

  1. Dump textures from the game:
       GE007_DUMP_SETTEX_TEXTURES='*' GE007_DUMP_SETTEX_DIR=/tmp/td \
         ./build/ge007 --level 33 ...            (static/world/HUD art, token-keyed)
     Only the static settex dump is runtime-loadable today: the in-game loader probes
     textures/tok%04d.png by settex token and has no hash-key path yet.

  2. Fetch the upscaler once:    tools/texpack/fetch_realesrgan.sh

  3. Build the pack:
       python3 tools/texpack/build_pack.py --dump /tmp/td --out /tmp/mypack

  4. Point the game at it (the in-game loader ships today):
       GE007_TEXTURE_PACK=/tmp/mypack ./build/ge007 ...

Output layout matches the loader key (docs/design/REMASTER_ROADMAP.md, Engine B):
  <out>/textures/tok####.png   (zero-padded 4-digit settex token; texture_pack.c:45)

Requires Pillow (pip install -r tools/texpack/requirements.txt) only to decode the
engine's .ppm dumps. Once the engine dumps PNG directly, Pillow is optional. The
heavy lifting is the bundled Real-ESRGAN ncnn-vulkan (GPU).
"""
import argparse, csv, json, os, re, shutil, subprocess, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN = os.path.join(HERE, ".bin", "realesrgan", "realesrgan-ncnn-vulkan")
DEFAULT_MODELS = os.path.join(HERE, ".bin", "realesrgan", "models")

# draw-class -> model. ESRGAN photo model for surfaces/props/characters; the
# "anime" model is often cleaner on flat/cel UI art. Routing is best-effort; if no
# manifest is present everything uses --model.
CLASS_MODEL = {
    "hud": "realesrgan-x4plus-anime",
    "room": "realesrgan-x4plus",
    "weapon": "realesrgan-x4plus",
    "chrprop": "realesrgan-x4plus",
    "effect": "realesrgan-x4plus",
}

TOKEN_RE = re.compile(r"(?:settex_|tok)(\d+)")


def normalize_token_key(raw):
    """Return the canonical runtime pack key `tok####` for a token string.
    THE one shared implementation (route_pack.py imports it): tolerant of
    trailing whitespace and non-str input (e.g. ints from hand-edited JSON)."""
    if raw is None:
        return None
    m = re.search(r"(?:settex_|tok)?(\d+)\s*$", str(raw).strip())
    if not m:
        return None
    return f"tok{int(m.group(1)):04d}"


def parse_token(fname):
    """Extract the runtime pack key from a static settex dump name."""
    m = TOKEN_RE.search(fname)
    if not m:
        return None
    return normalize_token_key(m.group(1))


def _pil():
    try:
        from PIL import Image
        return Image
    except ImportError:
        sys.exit("Pillow required: pip install -r tools/texpack/requirements.txt")


def load_rgba(src):
    """Decode an engine dump (.ppm/.png) to a PIL RGBA image.

    The engine dumps RGB to <tok>.rgba.ppm and the ALPHA channel SEPARATELY to
    <tok>.alpha.pgm (PPM has no alpha). We must re-attach it, or alpha-cutout
    textures lose their transparency through the pipeline: round wheels become
    solid squares, grates fill in, and glass goes opaque."""
    Image = _pil()
    im = Image.open(src).convert("RGBA")
    if src.endswith(".rgba.ppm"):
        alpha_path = src[:-len(".rgba.ppm")] + ".alpha.pgm"
        if os.path.exists(alpha_path):
            a = Image.open(alpha_path).convert("L")
            if a.size == im.size:
                im.putalpha(a)
    return im


def is_tileable(im, tol=20.0):
    """Heuristic: a texture wraps seamlessly if opposite edges roughly match
    (the right edge continues into the left, top into bottom). Such textures
    tile across large surfaces, so they must be upscaled seam-safe."""
    w, h = im.size
    if w < 8 or h < 8:
        return False
    px = im.load()

    def edge_cols(x0, x1):
        s = 0.0
        for y in range(h):
            a = px[x0, y]; b = px[x1, y]
            s += abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2])
        return s / (h * 3.0)

    def edge_rows(y0, y1):
        s = 0.0
        for x in range(w):
            a = px[x, y0]; b = px[x, y1]
            s += abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2])
        return s / (w * 3.0)

    return edge_cols(0, w - 1) < tol and edge_rows(0, h - 1) < tol


def tile_3x3(im):
    """Replicate a texture into a 3x3 grid so the upscaler sees the wrap context."""
    Image = _pil()
    w, h = im.size
    out = Image.new("RGBA", (w * 3, h * 3))
    for ty in range(3):
        for tx in range(3):
            out.paste(im, (tx * w, ty * h))
    return out


# --- W2.E3.T2: the distributable Tier-B refusal, SECOND enforcement point --------
# route_pack.py refuses at plan time; build_pack.py re-checks at BUILD time so a
# hand-edited plan.json cannot smuggle a Tier-B (AI-upscaled / tone-matched
# procedural) asset into a distributable pack. This is a DELIBERATELY INDEPENDENT
# copy of the source->tier rule (not an import from route_pack.py): the tier is
# judged from the entry's `source`, and any `tier` label the plan carries is
# ignored, so the two enforcement points can't share a single bypassable bug.

def _tone_mode(entry):
    """The `tone.mode` of a procedural entry, tolerating curator shorthand: a
    bare string tone T (e.g. `"tone": "match"`) means {"mode": T}. Any OTHER
    non-dict tone is malformed and returns "match" so the entry stays
    conservatively Tier B -- a malformed tone must never fail open to A1."""
    tone = entry.get("tone", {})
    if isinstance(tone, str):
        tone = {"mode": tone}
    if not isinstance(tone, dict):
        return "match"   # malformed -> conservatively Tier B
    return tone.get("mode")


def _entry_is_tier_b(entry):
    """True if `entry` resolves (by its SOURCE, never its `tier` label) to Tier B."""
    src = entry.get("source")
    if src in ("ai_upscale", "lanczos"):
        return True
    if src == "procedural":
        return _tone_mode(entry) == "match"
    if src in ("cc0_import", "original", "stock"):
        return False
    return True   # unknown/missing source is conservatively non-distributable


def plan_tier_b_violations(plan):
    """Sorted [(token, entry)] for every plan entry that is Tier B by source."""
    entries = plan.get("entries", {}) if isinstance(plan, dict) else {}
    return sorted(((t, e) for t, e in entries.items()
                   if isinstance(e, dict) and _entry_is_tier_b(e)),
                  key=lambda kv: kv[0])


def _format_plan_refusal(violations):
    lines = [f"REFUSED: --distributable pack contains {len(violations)} "
             f"Tier-B (non-redistributable) token(s):"]
    for tok, e in violations:
        lines.append(f"  {tok}: source={e.get('source', '?')} (Tier B) "
                     f"-> reroute to procedural (generic tone -> A1) or stock")
    lines.append("build_pack.py judges tier from the SOURCE, not the plan's `tier` "
                 "label, so a hand-edited label cannot bypass this gate.")
    return "\n".join(lines)


def load_manifest(dump_dir):
    """token -> draw_class, if a *.texmanifest.csv is present (manifest emit = §6 P1.2)."""
    cls = {}
    for mf in glob.glob(os.path.join(dump_dir, "*.texmanifest.csv")):
        with open(mf, newline="") as fp:
            reader = csv.reader(fp)
            header = None
            for row in reader:
                parts = [p.strip() for p in row]
                if not parts:
                    continue
                if header is None and any(p.lower() == "draw_class" for p in parts):
                    header = [p.lower() for p in parts]
                    continue
                key = normalize_token_key(parts[0])
                if key is None:
                    continue
                draw_class = ""
                if header and "draw_class" in header:
                    idx = header.index("draw_class")
                    if idx < len(parts):
                        draw_class = parts[idx]
                elif len(parts) >= 8:
                    draw_class = parts[7]
                elif len(parts) >= 7:
                    # Older scratch manifests had no tileable column.
                    draw_class = parts[6]
                if draw_class:
                    cls[key] = draw_class.lower()
    return cls


# --- W2.E2.T3: per-batch failure isolation + the plan-driven build -------------

def _run_ai_batches(by_model, tiled, stage, outdir, realesrgan, models, scale):
    """Run each per-model Real-ESRGAN batch with FAILURE ISOLATION (§4.5 item 4).

    The upscaler runs once per model (ncnn directory mode). The old code used
    `subprocess.run(check=True)`, so a single killed/failed batch raised and
    aborted the WHOLE pack -- every other token was lost. Here a batch that fails
    (upscaler killed, crashes, or exits non-zero) only records ITS tokens as
    failed; the remaining batches still build. The caller exits non-zero at the
    end with the failed-token summary. Returns the SORTED list of tokens that did
    not upscale (batches are processed in sorted model order for determinism).
    """
    Image = _pil()
    failed = []
    for model in sorted(by_model):
        toks = by_model[model]
        mdir = os.path.join(stage, "_in_" + model); os.makedirs(mdir, exist_ok=True)
        odir = os.path.join(stage, "_out_" + model); os.makedirs(odir, exist_ok=True)
        for t in toks:
            shutil.copyfile(os.path.join(stage, t + ".png"),
                            os.path.join(mdir, t + ".png"))
        try:
            rc = subprocess.run(
                [realesrgan, "-i", mdir, "-o", odir, "-s", str(scale),
                 "-n", model, "-m", models, "-f", "png"]).returncode
        except OSError as exc:
            print(f"  [batch {model}] upscaler could not be launched: {exc}",
                  file=sys.stderr)
            rc = -1
        if rc != 0:
            print(f"  [batch {model}] FAILED (upscaler exit {rc}); "
                  f"{len(toks)} token(s) not upscaled -- continuing with the "
                  f"remaining batches", file=sys.stderr)
            failed.extend(toks)
            continue
        for t in toks:
            up = os.path.join(odir, t + ".png")
            if not os.path.exists(up):
                # Batch reported success but this token's output is missing.
                print(f"  [batch {model}] {t}: upscaler produced no output",
                      file=sys.stderr)
                failed.append(t)
                continue
            if t in tiled:
                # seam-safe: the staged image was a 3x3 tiling; crop the center
                # tile so the upscaled result wraps seamlessly (no seams).
                ow, oh = tiled[t]; s_ = scale
                Image.open(up).crop(
                    (ow * s_, oh * s_, 2 * ow * s_, 2 * oh * s_)).save(
                        os.path.join(outdir, t + ".png"))
            else:
                shutil.move(up, os.path.join(outdir, t + ".png"))
    return sorted(failed)


def _report_failures(failed, outdir):
    """Print the failed-token summary and set up a non-zero exit (§4.5 item 4:
    a mid-batch failure NAMES the failed tokens; the build still exits non-zero)."""
    print(f"\nBUILD INCOMPLETE: {len(failed)} token(s) failed to build; the "
          f"remaining tokens were written to {outdir}.", file=sys.stderr)
    print(f"  failed: {' '.join(failed)}", file=sys.stderr)
    print("  the upscaler is deterministic -- re-run to retry the failed tokens.",
          file=sys.stderr)


def _build_from_plan(args):
    """Plan-driven build (W2.E2.T3, doc 02 §4.5 item 3): per-token source dispatch
    off a route_pack.py plan.json -- the PRIMARY build mode. The legacy
    manifest/--route dump build stays for quick one-offs.

    Sources built here: `ai_upscale` (whole_image / seam_safe) and `lanczos` --
    both read the source pixels from --dump -- and `stock` (skipped; keeps the
    game's own texture). `procedural` (W2.E5) and `cc0_import` (W2.E4.T3) are
    A-tier sources owned by sibling tasks; they are recognized and DEFERRED here
    (named, skipped, non-fatal) until those build paths land. Shares the
    failure-isolated ESRGAN batch runner with the legacy path.

    --distributable NEVER reaches here: it is handled fail-closed (Tier-B refusal)
    in main() before this dispatch, so a plan-driven build is always a
    non-distributable "full" pack; the distributable A-tier build lands with the
    procedural/cc0 sources (W2.E4/E5).
    """
    if not args.out:
        sys.exit("--plan build requires --out (the output pack dir)")
    with open(args.plan) as fp:
        plan = json.load(fp)
    entries = plan.get("entries", {}) if isinstance(plan, dict) else {}

    # token -> dump source file, discovered EXACTLY like the legacy build (same
    # glob, same parse_token), so the SAME source pixels feed both paths -- the
    # basis for the byte-identical-per-file equivalence (acceptance leg 2).
    src_by_tok = {}
    if args.dump:
        for s in sorted(glob.glob(os.path.join(args.dump, "*.rgba.ppm")) +
                        glob.glob(os.path.join(args.dump, "*.png"))):
            tok = parse_token(os.path.basename(s))
            if tok and tok not in src_by_tok:
                src_by_tok[tok] = s

    Image = _pil()
    outdir = os.path.join(args.out, "textures"); os.makedirs(outdir, exist_ok=True)
    stage = os.path.join(args.out, ".stage"); os.makedirs(stage, exist_ok=True)

    by_model = {}       # model -> [tokens] for the ESRGAN batch
    tiled = {}          # token -> (orig_w, orig_h) for the seam-safe crop
    built = lanczos_n = stock_n = 0
    deferred = []       # (token, source) recognized A-tier sources (built by E4/E5)
    missing_src = []    # ai/lanczos tokens with no matching dump file (hard fail)
    bad_source = []     # unrecognized source strings (hard fail)

    for tok in sorted(entries):
        entry = entries[tok]
        if not isinstance(entry, dict):
            continue
        src = entry.get("source")
        if src == "stock":
            stock_n += 1
            continue
        if src in ("procedural", "cc0_import", "original"):
            deferred.append((tok, src))
            continue
        if src not in ("ai_upscale", "lanczos"):
            bad_source.append((tok, src if src else "(missing)"))
            continue
        # ai_upscale / lanczos both need the ROM dump pixels for this token.
        s = src_by_tok.get(tok)
        if s is None:
            missing_src.append(tok)
            continue
        im = load_rgba(s)
        w, h = im.size
        if src == "lanczos":
            # Deterministic Lanczos (matches the legacy tiny-texture path exactly).
            im.resize((w * args.scale, h * args.scale), Image.LANCZOS).save(
                os.path.join(outdir, tok + ".png"))
            lanczos_n += 1; built += 1
            continue
        # ai_upscale: honor the plan's routing decision. seam_safe iff the router
        # judged the token tileable -- that decision came from the SAME is_tileable
        # metric the legacy path recomputes (W2.E1.T2 C/Python parity), so the two
        # paths agree on which tokens get the 3x3 seam-safe staging.
        mode = entry.get("mode", "whole_image")
        seamless = (not args.no_seamless) and (mode == "seam_safe")
        staged = tile_3x3(im) if seamless else im
        if seamless:
            tiled[tok] = (w, h)
        staged.save(os.path.join(stage, tok + ".png"))
        model = entry.get("model") or args.model
        by_model.setdefault(model, []).append(tok)
        built += 1

    if by_model and not (os.path.isfile(args.realesrgan) and
                         os.access(args.realesrgan, os.X_OK)):
        sys.exit(f"Real-ESRGAN not found at {args.realesrgan}\n  "
                 f"run tools/texpack/fetch_realesrgan.sh first")

    for tok, src in deferred:
        print(f"  DEFER {tok}: source={src} is an A-tier source built by "
              f"W2.E4.T3/E5, not by this tool version -- skipped", file=sys.stderr)
    for tok, src in bad_source:
        print(f"  WARN {tok}: unrecognized source {src!r} -- skipped",
              file=sys.stderr)
    for tok in missing_src:
        print(f"  WARN {tok}: no matching texture in --dump "
              f"{args.dump or '(none given)'} -- skipped", file=sys.stderr)

    print(f"plan build: {built} token(s) staged "
          f"({lanczos_n} lanczos, {len(tiled)} seam-safe, {stock_n} stock/skip, "
          f"{len(deferred)} deferred to E4/E5); AI-upscaling x{args.scale} with: "
          f"{', '.join(sorted(by_model)) or '(none)'}")

    failed = _run_ai_batches(by_model, tiled, stage, outdir,
                             args.realesrgan, args.models, args.scale)
    shutil.rmtree(stage, ignore_errors=True)

    # Exit non-zero if any token we WERE asked to build did not build: an upscaler
    # batch failure, a missing dump source, or an unrecognized source. Deferred
    # A-tier sources are NOT failures -- they are owned by sibling tasks (E4/E5).
    hard_failures = sorted(failed + missing_src + [t for t, _ in bad_source])
    if hard_failures:
        _report_failures(hard_failures, outdir)
        sys.exit(1)
    print(f"pack ready: {outdir}  ({built} textures)")
    print("  (HD packs are ROM-derived -- keep them local, never commit.)")


def main():
    ap = argparse.ArgumentParser(description="Build an HD texture pack from a MGB64 dump via Real-ESRGAN.")
    ap.add_argument("--dump", help="texture dump dir (GE007_DUMP_*_DIR output)")
    ap.add_argument("--out", help="output pack dir (creates <out>/textures/)")
    ap.add_argument("--scale", type=int, default=4, choices=[2, 3, 4])
    ap.add_argument("--model", default="realesrgan-x4plus", help="default model when no manifest routing")
    ap.add_argument("--realesrgan", default=DEFAULT_BIN)
    ap.add_argument("--models", default=DEFAULT_MODELS)
    ap.add_argument("--route", action="store_true", help="route model by draw_class via *.texmanifest.csv")
    ap.add_argument("--no-seamless", action="store_true",
                    help="disable seam-safe (tile-and-crop) upscaling for tileable textures")
    ap.add_argument("--plan", help="route_pack.py plan.json: build the pack by "
                                   "per-token source dispatch (ai_upscale / lanczos "
                                   "read pixels from --dump; stock is skipped; "
                                   "procedural + cc0_import are deferred to W2.E4/E5). "
                                   "Also re-checked by --distributable (Tier-B gate).")
    ap.add_argument("--distributable", action="store_true",
                    help="refuse (exit 1) if any --plan entry is Tier B by source; "
                         "gate check ONLY -- refuses to run the dump-driven build "
                         "(it would emit Tier-B assets regardless of the plan)")
    args = ap.parse_args()

    # W2.E3.T2: the build-time Tier-B refusal (second enforcement point). Runs
    # BEFORE any build work so a rejected distributable pack produces nothing.
    if args.distributable:
        if not args.plan:
            sys.exit("--distributable requires --plan (the plan.json to re-check)")
        with open(args.plan) as fp:
            plan = json.load(fp)
        violations = plan_tier_b_violations(plan)
        if violations:
            sys.exit(_format_plan_refusal(violations))
        n = len(plan.get("entries", {})) if isinstance(plan, dict) else 0
        print(f"distributable Tier-B check passed ({n} entries, all A-tier)")
        # FAIL CLOSED: the legacy dump-driven pipeline below IGNORES the plan and
        # AI-upscales every ROM dump it finds -- i.e. it emits Tier-B assets no
        # matter what the plan says. Until the plan-driven build (W2.E2.T3) lands,
        # --distributable is the gate check ONLY and refuses to build.
        if args.dump:
            sys.exit("REFUSED: --distributable cannot run the dump-driven build: "
                     "that pipeline ignores the validated plan and emits Tier-B "
                     "(AI-upscaled ROM-derived) assets. A plan-driven build "
                     "(W2.E2.T3) is required to emit a distributable pack; drop "
                     "--dump to run the Tier-B gate check alone.")
        # Nothing further to build here: the plan-driven build below emits
        # Tier-B (ai_upscale/lanczos) assets from the dump, and the A-tier
        # distributable build (procedural/cc0) lands with W2.E4/E5.
        print("  (gate check only; the distributable A-tier build lands with W2.E4/E5)")
        return

    # W2.E2.T3: the plan-driven build (the PRIMARY build mode). Runs only WITHOUT
    # --distributable (that stays fail-closed above), so a plan build is always a
    # non-distributable "full" pack.
    if args.plan:
        _build_from_plan(args)
        return

    if not args.dump or not args.out:
        sys.exit("--dump and --out are required to build a pack "
                 "(or use --plan --distributable for the Tier-B gate only)")

    if not (os.path.isfile(args.realesrgan) and os.access(args.realesrgan, os.X_OK)):
        sys.exit(f"Real-ESRGAN not found at {args.realesrgan}\n  run tools/texpack/fetch_realesrgan.sh first")

    srcs = sorted(glob.glob(os.path.join(args.dump, "*.rgba.ppm")) +
                  glob.glob(os.path.join(args.dump, "*.png")))
    if not srcs:
        sys.exit(f"no *.rgba.ppm / *.png dumps in {args.dump}")

    Image = _pil()
    manifest = load_manifest(args.dump) if args.route else {}
    stage = os.path.join(args.out, ".stage"); os.makedirs(stage, exist_ok=True)
    outdir = os.path.join(args.out, "textures"); os.makedirs(outdir, exist_ok=True)
    TINY = 16   # source textures this small or smaller go through Lanczos, not the AI
                # upscaler — Real-ESRGAN hallucinates faces/junk on tiny ambiguous tiles.

    by_model = {}          # model -> [tokens] for the ESRGAN batch
    tiled = {}             # token -> (orig_w, orig_h) for seam-safe crop after upscale
    n = lanczos_n = skipped_n = 0
    for s in srcs:
        tok = parse_token(os.path.basename(s))
        if not tok:
            skipped_n += 1
            continue
        im = load_rgba(s)
        w, h = im.size
        if max(w, h) <= TINY:
            # anti-hallucination: deterministic Lanczos for tiny textures
            im.resize((w * args.scale, h * args.scale), Image.LANCZOS).save(
                os.path.join(outdir, tok + ".png"))
            lanczos_n += 1; n += 1
            continue
        seamless = (not args.no_seamless) and is_tileable(im)
        staged = tile_3x3(im) if seamless else im
        if seamless:
            tiled[tok] = (w, h)
        staged.save(os.path.join(stage, tok + ".png"))
        model = CLASS_MODEL.get(manifest.get(tok, ""), args.model)
        by_model.setdefault(model, []).append(tok)
        n += 1

    if n == 0:
        sys.exit("no runtime-loadable static settex dumps found; use GE007_DUMP_SETTEX_TEXTURES, "
                 "not GE007_DUMP_LOADED_TEXTURES/hash-key dumps")

    print(f"staged {n} textures ({lanczos_n} tiny->Lanczos, {len(tiled)} tileable->seam-safe, "
          f"{skipped_n} skipped non-settex/hash-key dumps); "
          f"AI-upscaling x{args.scale} with: {', '.join(by_model) or '(none)'}")
    failed = _run_ai_batches(by_model, tiled, stage, outdir,
                             args.realesrgan, args.models, args.scale)
    shutil.rmtree(stage, ignore_errors=True)
    if failed:
        _report_failures(failed, outdir)
        sys.exit(1)
    print(f"pack ready: {outdir}  ({n} textures)")
    print("  (HD packs are ROM-derived -- keep them local, never commit.)")


if __name__ == "__main__":
    main()
