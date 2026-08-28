#!/usr/bin/env python3
"""route_pack.py -- the MGB64 HD-pack Router (W2.E3, docs/remaster-aaa 02 §4.2).

Deterministic function:  (manifest.csv, overrides.json, library index) -> plan.json

The manifest is what W2.E1 emits (`token,w,h,fmt,siz,avgRGB,tileable,draw_class`);
the plan is the per-token remaster decision that build_pack.py then executes. The
plan is BYTE-STABLE across runs (sorted keys, no floats) and ROM-free (only token
ids + routing decisions), so unlike the manifest CSV it is committable.

Decision tree (evaluated top-down per token, §4.2):

  1. token in overrides.json            -> use override verbatim (source/args)
  2. draw_class not in {room}           -> ai_upscale, mode=whole_image
  3. draw_class == room:
     a. max(w,h) <= 16                  -> lanczos           (tiny; AI hallucinates)
     b. tileable && max(w,h) <= 64      -> UNROUTED small tile:
          a default preset would be a guess (a mis-preset floor is worse than a
          blocky one), so emit source=stock + a WARN. Curation (W2.E8) fills these.
     c. tileable && library match       -> cc0_import        (curated, never auto)
     d. tileable, no match              -> ai_upscale, mode=seam_safe
     e. not tileable                    -> ai_upscale, mode=whole_image
  4. overrides may also set source=stock (e.g. Dam rock tok0949: per-quad-UV seam
     surfaces stay stock until W1 smooth normals) -- handled by branch 1 verbatim.

Tier rules (hard-coded, NOT configurable, §4.2): ai_upscale/lanczos = B always;
procedural = B if tone.mode=="match" else A1; cc0_import = A1 (CC0/PD) or A2
(CC-BY*) from provenance; original = A1; stock = "-" (distributable-safe).

The refusal invariant (R2): `--distributable` exits non-zero (writing no plan) if
ANY entry resolves to Tier B, naming every offending token and its cheapest A-tier
alternative. The tier is judged from the SOURCE (a `tier` label on an override/plan
is never trusted) here AND re-checked from source in build_pack.py -- defense in
depth, so a hand-edited `tier:"A1"` label cannot smuggle a Tier-B asset into a pack.
"""
import argparse
import csv
import glob
import json
import os
import re
import sys
from collections import namedtuple

# token, w, h are all the tree needs from the geometry columns; avgRGB is
# ROM-derived and deliberately NOT carried (keeps the Row -- and thus any
# accidental emit -- ROM-free). fmt/siz retained for future routing hooks.
Row = namedtuple("Row", ["token", "w", "h", "fmt", "siz", "tileable", "draw_class"])

TINY = 16   # <= this on the long edge: Lanczos, not AI (matches build_pack.py:TINY)
SMALL = 64  # tileable ROOM tiles <= this have no AI-recoverable detail (§4.2 3b)

# The token-key canonicalizer is shared with the sibling build_pack.py (ONE
# implementation, no drift). Both tools run as __main__ from this directory, so
# make it importable regardless of how THIS file was loaded (script or pytest).
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from build_pack import normalize_token_key  # noqa: E402


# --------------------------------------------------------------------------- IO

def load_manifest(path):
    """Parse a W2.E1 texmanifest CSV -> {token: Row}. Header-driven (tolerant of
    column reordering), matching build_pack.py's manifest reader."""
    manifest = {}
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
                # No header seen yet and this isn't one -> skip (defensive).
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
            try:
                w = int(col("w", "0")); h = int(col("h", "0"))
                fmt = int(col("fmt", "0")); siz = int(col("siz", "0"))
                tileable = int(col("tileable", "0"))
            except ValueError:
                continue
            manifest[tok] = Row(tok, w, h, fmt, siz, tileable,
                                col("draw_class", "").lower())
    return manifest


def load_overrides(path):
    """Load an overrides JSON: {token: {source, ...args}}. None/'' -> {}."""
    if not path:
        return {}
    with open(path) as fp:
        raw = json.load(fp)
    out = {}
    for k, v in raw.items():
        tok = normalize_token_key(k)
        if tok is None or not isinstance(v, dict):
            continue
        out[tok] = dict(v)
    return out


class LibraryIndex:
    """The curated open-licensed asset index (W2.E4 builds the real one; M1
    predates it, so `--library` is OPTIONAL and library=None => branch 3c never
    fires). Only the operational shape the Router needs is defined here: a curated
    per-token mapping to a cc0/cc-by asset with its provenance tier. E4 finalizes
    the on-disk schema; a `token_map` object ({tok####: {asset, tier}}) in
    index.json is honored so branch 3c is exercisable today."""

    def __init__(self, token_map):
        self._map = token_map

    @classmethod
    def load(cls, dir_path):
        idx = os.path.join(dir_path, "index.json")
        with open(idx) as fp:
            data = json.load(fp)
        raw = data.get("token_map", {}) if isinstance(data, dict) else {}
        token_map = {}
        for k, v in raw.items():
            tok = normalize_token_key(k)
            if tok is None or not isinstance(v, dict) or "asset" not in v:
                continue
            token_map[tok] = v
        return cls(token_map)

    def match(self, token):
        """Curated record {asset, tier?} for `token`, or None. Never automatic."""
        return self._map.get(token)


# ------------------------------------------------------------------- tier rules

def _tone_mode(entry):
    """The `tone.mode` of a procedural entry, tolerating curator shorthand: a
    bare string tone T (e.g. `"tone": "match"`) means {"mode": T}. Any OTHER
    non-dict tone is malformed and returns "match" so the entry stays
    conservatively Tier B -- a malformed tone must never fail open to A1.
    (build_pack.py carries its own copy: the tier rule is deliberately NOT
    shared between the two enforcement points.)"""
    tone = entry.get("tone", {})
    if isinstance(tone, str):
        tone = {"mode": tone}
    if not isinstance(tone, dict):
        return "match"   # malformed -> conservatively Tier B
    return tone.get("mode")


def compute_tier(entry):
    """Derive the licensing tier from the ENTRY'S SOURCE (hard-coded §4.2 rules).
    This is authoritative: any `tier` a hand-edited plan carries is IGNORED for the
    security-relevant sources (ai_upscale/lanczos/procedural), so a smuggled label
    cannot upgrade a Tier-B asset."""
    src = entry.get("source")
    if src in ("ai_upscale", "lanczos"):
        return "B"
    if src == "procedural":
        return "B" if _tone_mode(entry) == "match" else "A1"
    if src == "cc0_import":
        # A1 (CC0/PD) or A2 (CC-BY*) is a provenance fact from the library record,
        # not a security-relevant label (cc0_import is never Tier B either way).
        t = entry.get("tier")
        return t if t in ("A1", "A2") else "A1"
    if src == "original":
        return "A1"
    if src == "stock":
        return "-"
    # Unknown/malformed source -> treat as B so --distributable refuses it (T2).
    return "B"


def is_tier_b(entry):
    """Source-based Tier-B test (ignores any `tier` label on the entry). Defined
    AS compute_tier so the rule table has a single source of truth here;
    build_pack.py re-derives it independently (defense in depth)."""
    return compute_tier(entry) == "B"


def cheapest_alternative(entry):
    """The A-tier route a curator should switch a refused token to (§4.2)."""
    if entry.get("source") == "procedural":
        # Tier B here can only mean tone.mode == "match" (tone-locked to ROM
        # pixels); the same preset with a generic tone is A1.
        return "the same procedural preset with tone.mode=generic (-> A1)"
    return "procedural (generic tone -> A1) or stock"


# ------------------------------------------------------------------ the router

def _route_token(token, row, overrides, library, warnings):
    """Route ONE token per the §4.2 tree. Returns the entry dict (tier stamped)."""
    # 1. Override wins verbatim (source + its args); tier is still recomputed from
    #    the source below (hard-coded rules are not overridable).
    if token in overrides:
        entry = dict(overrides[token])
    elif row.draw_class != "room":
        # 2. Non-room art -> whole-image AI upscale.
        entry = {"source": "ai_upscale", "mode": "whole_image",
                 "model": "realesrgan-x4plus"}
    else:
        longest = max(row.w, row.h)
        if longest <= TINY:
            # 3a. Tiny -> deterministic Lanczos (AI hallucinates on tiny tiles).
            entry = {"source": "lanczos"}
        elif row.tileable and longest <= SMALL:
            # 3b. Small tileable ROOM tile, unrouted. A default preset is a GUESS;
            #     WARN and fall back to stock -- curation (W2.E8) fills these in.
            warnings.append(
                f"WARN: {token} is a small tileable ROOM tile "
                f"({row.w}x{row.h}) with no override -> source=stock. "
                f"A guessed procedural preset is worse than stock; "
                f"curate it in overrides (W2.E8).")
            entry = {"source": "stock",
                     "reason": "unrouted small tileable ROOM tile; "
                               "curate in overrides (W2.E8)"}
        elif row.tileable and library is not None and library.match(token):
            # 3c. Curated cc0 import (never automatic; a human mapping).
            rec = library.match(token)
            entry = {"source": "cc0_import", "asset": rec["asset"]}
            if rec.get("tier") in ("A1", "A2"):
                entry["tier"] = rec["tier"]
        elif row.tileable:
            # 3d. Tileable, no curated match -> seam-safe AI upscale.
            entry = {"source": "ai_upscale", "mode": "seam_safe",
                     "model": "realesrgan-x4plus"}
        else:
            # 3e. Non-tileable ROOM art -> whole-image AI upscale.
            entry = {"source": "ai_upscale", "mode": "whole_image",
                     "model": "realesrgan-x4plus"}

    entry["tier"] = compute_tier(entry)
    return entry


def route(manifest, overrides, library, distributable):
    """(manifest, overrides, library, distributable) -> Plan (§4.2 signature).

    Deterministic: tokens are routed in sorted order and the returned dict is
    serialized with sorted keys, so route(x) == route(x) byte-for-byte. WARN lines
    (unrouted small tiles) ride under the transient `_warnings` key, which
    serialize_plan() strips -- plan.json itself stays clean and byte-stable."""
    overrides = overrides or {}
    warnings = []
    entries = {}
    for token in sorted(manifest):
        entries[token] = _route_token(token, manifest[token], overrides,
                                      library, warnings)
    # Only manifest tokens are routed, so an override for a token the manifest
    # lacks would otherwise vanish silently (typo'd id / wrong level's manifest).
    dropped = sorted(set(overrides) - set(manifest))
    if dropped:
        warnings.append(
            f"WARN: {len(dropped)} override token(s) not in the manifest, "
            f"dropped from the plan: {', '.join(dropped)}. "
            f"Check the token ids / that this is the right level's manifest.")
    return {
        "pack_kind": "distributable" if distributable else "full",
        "entries": entries,
        "_warnings": warnings,
    }


# ----------------------------------------------------------- distributable gate

def distributable_violations(plan):
    """Sorted [(token, entry)] for every entry that resolves (by SOURCE) to Tier
    B -- the tokens that must not ship in a distributable pack."""
    return sorted(((t, e) for t, e in plan.get("entries", {}).items()
                   if is_tier_b(e)), key=lambda kv: kv[0])


def format_refusal(violations):
    """The actionable refusal message (names each token + its A-tier alternative)."""
    lines = [f"REFUSED: --distributable pack contains {len(violations)} "
             f"Tier-B (non-redistributable) token(s):"]
    for tok, e in violations:
        src = e.get("source", "?")
        lines.append(f"  {tok}: source={src} (Tier B) "
                     f"-> reroute to {cheapest_alternative(e)}")
    lines.append("Tier-B assets (AI-upscaled / tone-matched procedural) are "
                 "ROM-derived or tone-locked and may not be redistributed.")
    return "\n".join(lines)


# ----------------------------------------------------------- cross-plan conflict

_SIG_KEYS = ("source", "preset", "asset", "mode", "reason", "args", "license")


def _route_sig(d):
    """Normalized routing signature (source + meaningful args) for conflict
    comparison. Ignores derived fields (tier) and size/model cosmetics.
    `args`/`license` (cc0_import tone + provenance) ARE meaningful: two levels
    sharing a global token with different tone args would silently clobber
    each other's build in the shared pack dir. Signatures are only ever
    compared with ==/!=, so a dict-valued key is fine here."""
    return tuple(d.get(k) for k in _SIG_KEYS)


def conflict_warnings(entries, level, other_sources):
    """WARN when a token in THIS plan is routed differently in a previously
    written plan/override for the SAME token (§4.7 -- the shared pack dir means the
    last build silently wins, so contradictory routes must surface).

    `other_sources` is a sorted list of (label, {token: route_dict}) drawn from the
    sibling `overrides/*.json` and `plan_*.json`. Output order is deterministic."""
    warns = []
    for tok in sorted(entries):
        mine = _route_sig(entries[tok])
        for label, table in other_sources:
            if tok in table:
                theirs = _route_sig(table[tok])
                if theirs != mine:
                    warns.append(
                        f"WARN: shared-token conflict on {tok}: {level} routes "
                        f"source={mine[0]} but {label} routes source={theirs[0]}. "
                        f"The pack dir is shared (global token ids) -> last build "
                        f"wins. Pick one route and note it in both overrides.")
    return warns


def _gather_other_sources(overrides_dir, level, out_path):
    """Collect (label, {token: route_dict}) from sibling overrides + plan JSONs,
    excluding this level's own override and this run's output. Sorted for
    determinism."""
    others = []
    if overrides_dir and os.path.isdir(overrides_dir):
        for p in sorted(glob.glob(os.path.join(overrides_dir, "*.json"))):
            name = os.path.splitext(os.path.basename(p))[0]
            if name == level:
                continue
            try:
                raw = json.load(open(p))
            except (OSError, json.JSONDecodeError):
                continue
            table = {normalize_token_key(k): v for k, v in raw.items()
                     if normalize_token_key(k) and isinstance(v, dict)}
            others.append((f"overrides/{name}.json", table))
    out_dir = os.path.dirname(os.path.abspath(out_path)) if out_path else None
    if out_dir and os.path.isdir(out_dir):
        self_abs = os.path.abspath(out_path)
        for p in sorted(glob.glob(os.path.join(out_dir, "plan_*.json"))):
            if os.path.abspath(p) == self_abs:
                continue
            try:
                raw = json.load(open(p))
            except (OSError, json.JSONDecodeError):
                continue
            table = {normalize_token_key(k): v
                     for k, v in raw.get("entries", {}).items()
                     if normalize_token_key(k) and isinstance(v, dict)}
            others.append((os.path.basename(p), table))
    return sorted(others, key=lambda kv: kv[0])


# --------------------------------------------------------------- serialization

def _strip_transient(obj):
    """Drop `_`-prefixed transient keys (e.g. `_warnings`) so plan.json is clean."""
    if isinstance(obj, dict):
        return {k: _strip_transient(v) for k, v in obj.items()
                if not (isinstance(k, str) and k.startswith("_"))}
    if isinstance(obj, list):
        return [_strip_transient(v) for v in obj]
    return obj


def serialize_plan(plan):
    """Byte-stable, float-free JSON text (sorted keys, transient keys stripped,
    trailing newline). Identical input -> identical bytes across runs."""
    return json.dumps(_strip_transient(plan), sort_keys=True, indent=2,
                      ensure_ascii=True) + "\n"


# ---------------------------------------------------------------------- driver

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Route a MGB64 texmanifest into a deterministic pack plan.")
    ap.add_argument("--manifest", required=True,
                    help="W2.E1 texmanifest CSV (token,w,h,fmt,siz,avgRGB,"
                         "tileable,draw_class)")
    ap.add_argument("--overrides", help="per-level overrides JSON (optional)")
    ap.add_argument("--out", required=True, help="output plan.json path")
    ap.add_argument("--level", required=True, help="level slug (recorded in plan)")
    ap.add_argument("--library", help="cc0 library dir (reads <dir>/index.json; "
                                      "OPTIONAL -- M1 predates it)")
    ap.add_argument("--distributable", action="store_true",
                    help="refuse (exit 1) if any entry resolves to Tier B")
    args = ap.parse_args(argv)

    manifest = load_manifest(args.manifest)
    overrides = load_overrides(args.overrides)
    library = LibraryIndex.load(args.library) if args.library else None

    plan = route(manifest, overrides, library, args.distributable)
    plan["level"] = args.level

    # WARN-not-guess: unrouted small ROOM tiles (from route()).
    for w in plan.get("_warnings", []):
        print(w, file=sys.stderr)

    # Cross-plan shared-token conflict WARNs (§4.7).
    overrides_dir = (os.path.dirname(os.path.abspath(args.overrides))
                     if args.overrides else None)
    others = _gather_other_sources(overrides_dir, args.level, args.out)
    for w in conflict_warnings(plan["entries"], args.level, others):
        print(w, file=sys.stderr)

    # The refusal invariant (R2): a distributable pack must be Tier-B-free. Refuse
    # WITHOUT writing the plan so a rejected distributable pack can't slip through.
    if args.distributable:
        violations = distributable_violations(plan)
        if violations:
            print(format_refusal(violations), file=sys.stderr)
            return 1

    with open(args.out, "w") as fp:
        fp.write(serialize_plan(plan))
    print(f"wrote {args.out} ({len(plan['entries'])} tokens, "
          f"pack_kind={plan['pack_kind']})", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
