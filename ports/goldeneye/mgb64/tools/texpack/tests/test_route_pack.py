"""W2.E3.T1/T2 -- route_pack.py decision-tree, determinism, tier + refusal tests.

Every branch of the §4.2 tree (1 / 2 / 3a / 3b / 3c / 3d / 3e / 4) is exercised by a
synthetic manifest built in-test (no ROM data). Determinism is asserted byte-for-byte
(`route(x) == route(x)` AND identical serialized bytes across runs and across a full
`main()` re-run). The committed `overrides/dam.json` is proven to reproduce the shipped
Dam decisions (tok0022 procedural, tok0949 stock) against a synthetic Dam-token
manifest -- the real manifest is ROM-derived/gitignored, so the curation file (not ROM
pixels) is the thing under test here. The Tier-B `--distributable` refusal invariant
(R2) is judged from the SOURCE at both enforcement points (route_pack.py at plan time,
build_pack.py at build time) so a smuggled `tier` label cannot bypass it.
"""
import importlib.util
import json
import sys
from pathlib import Path

import pytest

TESTS_DIR = Path(__file__).resolve().parent
ROUTE_PACK = TESTS_DIR.parent / "route_pack.py"
OVERRIDES_DIR = TESTS_DIR.parent / "overrides"
BUILD_PACK = TESTS_DIR.parent / "build_pack.py"


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


rp = _load(ROUTE_PACK, "route_pack")


# --------------------------------------------------------------------- helpers

def row(token, w, h, draw_class, tileable, fmt=0, siz=0):
    return rp.Row(rp.normalize_token_key(token), w, h, fmt, siz, tileable,
                  draw_class)


def manifest(*rows):
    return {r.token: r for r in rows}


def route1(r, overrides=None, library=None, distributable=False):
    """Route a single-row manifest and return that one entry."""
    plan = rp.route(manifest(r), overrides or {}, library, distributable)
    return plan["entries"][r.token]


def lib(token_map):
    return rp.LibraryIndex({rp.normalize_token_key(k): v
                            for k, v in token_map.items()})


# --------------------------------------------------- decision-tree branch cover
# 10 synthetic single-token manifests, one per reachable branch (some branches get
# two scenarios). Each asserts the routed source/mode + the derived tier.

def test_branch1_override_verbatim():
    # 1. token in overrides -> use override verbatim (source + args).
    ov = {"tok0311": {"source": "cc0_import", "asset": "ambientcg/Metal032",
                      "tier": "A1"}}
    e = route1(row("tok0311", 128, 128, "room", 1), overrides=ov)
    assert e["source"] == "cc0_import"
    assert e["asset"] == "ambientcg/Metal032"
    assert e["tier"] == "A1"


def test_branch4_override_stock():
    # 4. overrides may set source=stock (Dam rock case) -> verbatim, Tier '-'.
    ov = {"tok0949": {"source": "stock", "reason": "per-quad UV seams"}}
    e = route1(row("tok0949", 64, 64, "room", 0), overrides=ov)
    assert e["source"] == "stock"
    assert e["reason"] == "per-quad UV seams"
    assert e["tier"] == "-"


def test_branch1_override_ignores_smuggled_tier():
    # An override cannot upgrade a Tier-B source by carrying a fake `tier` label;
    # the tier is recomputed from the source (hard-coded rules win).
    ov = {"tok0007": {"source": "ai_upscale", "mode": "whole_image",
                      "tier": "A1"}}
    e = route1(row("tok0007", 128, 128, "room", 0), overrides=ov)
    assert e["source"] == "ai_upscale"
    assert e["tier"] == "B", "smuggled tier:A1 must be overwritten to B"


def test_branch2_non_room():
    # 2. draw_class not in {room} -> whole-image AI upscale.
    for cls in ("hud", "weapon", "chrprop", "effect", "unknown"):
        e = route1(row("tok0100", 64, 64, cls, 1))
        assert e["source"] == "ai_upscale" and e["mode"] == "whole_image"
        assert e["tier"] == "B"


def test_branch3a_tiny_room_lanczos():
    # 3a. room, max(w,h) <= 16 -> lanczos (AI hallucinates on tiny tiles).
    for w, h in ((16, 16), (8, 16), (16, 4), (1, 1)):
        e = route1(row("tok0200", w, h, "room", 1))
        assert e["source"] == "lanczos", f"{w}x{h} should be lanczos"
        assert e["tier"] == "B"


def test_branch3b_small_tileable_unrouted_warns():
    # 3b. room, tileable, 16 < max <= 64, no override -> STOCK + WARN (not a guess).
    warns = []
    e = rp._route_token("tok0300", row("tok0300", 64, 32, "room", 1),
                        {}, None, warns)
    assert e["source"] == "stock", "unrouted small tileable ROOM tile -> stock"
    assert e["tier"] == "-"
    # WARN-not-guess: it must NOT invent a procedural preset.
    assert "preset" not in e
    assert len(warns) == 1 and "tok0300" in warns[0] and "WARN" in warns[0]


def test_branch3c_library_match_cc0():
    # 3c. room, tileable, max > 64, curated library match -> cc0_import.
    library = lib({"tok0311": {"asset": "ambientcg/Metal032", "tier": "A1"}})
    e = route1(row("tok0311", 128, 128, "room", 1), library=library)
    assert e["source"] == "cc0_import" and e["asset"] == "ambientcg/Metal032"
    assert e["tier"] == "A1"


def test_branch3c_cc_by_is_a2():
    library = lib({"tok0312": {"asset": "polyhaven/rock", "tier": "A2"}})
    e = route1(row("tok0312", 256, 256, "room", 1), library=library)
    assert e["source"] == "cc0_import" and e["tier"] == "A2"


def test_branch3d_tileable_no_match_seamsafe():
    # 3d. room, tileable, max > 64, no curated match -> seam-safe AI upscale.
    # library=None => branch 3c can never match.
    e = route1(row("tok0400", 128, 128, "room", 1), library=None)
    assert e["source"] == "ai_upscale" and e["mode"] == "seam_safe"
    assert e["tier"] == "B"
    # Also with a library that simply lacks this token.
    e2 = route1(row("tok0400", 128, 128, "room", 1),
                library=lib({"tok9999": {"asset": "x"}}))
    assert e2["source"] == "ai_upscale" and e2["mode"] == "seam_safe"


def test_branch3e_nontileable_room_whole_image():
    # 3e. room, not tileable -> whole-image AI upscale.
    e = route1(row("tok0500", 128, 128, "room", 0))
    assert e["source"] == "ai_upscale" and e["mode"] == "whole_image"
    assert e["tier"] == "B"


def test_branch_precedence_3b_before_3c():
    # A small (max<=64) tileable tile WITH a library match still hits 3b first
    # (top-down), because tiny tiles have no AI-recoverable detail -- cc0 import is
    # for large tiles; use an override to force cc0 on a small one.
    library = lib({"tok0600": {"asset": "ambientcg/Foo", "tier": "A1"}})
    e = route1(row("tok0600", 64, 64, "room", 1), library=library)
    assert e["source"] == "stock", "3b precedes 3c for small tiles"


# --------------------------------------------------------------- tier rule table

@pytest.mark.parametrize("entry,expected", [
    ({"source": "ai_upscale", "mode": "whole_image"}, "B"),
    ({"source": "lanczos"}, "B"),
    ({"source": "procedural", "tone": {"mode": "match"}}, "B"),
    ({"source": "procedural", "tone": {"mode": "generic"}}, "A1"),
    ({"source": "procedural"}, "A1"),
    ({"source": "cc0_import", "asset": "x", "tier": "A1"}, "A1"),
    ({"source": "cc0_import", "asset": "x", "tier": "A2"}, "A2"),
    ({"source": "cc0_import", "asset": "x"}, "A1"),
    ({"source": "original"}, "A1"),
    ({"source": "stock", "reason": "y"}, "-"),
])
def test_compute_tier_rules(entry, expected):
    assert rp.compute_tier(entry) == expected


@pytest.mark.parametrize("entry,is_b", [
    ({"source": "ai_upscale"}, True),
    ({"source": "lanczos"}, True),
    ({"source": "procedural", "tone": {"mode": "match"}}, True),
    ({"source": "procedural", "tone": {"mode": "generic"}}, False),
    ({"source": "procedural"}, False),
    ({"source": "cc0_import", "asset": "x"}, False),
    ({"source": "original"}, False),
    ({"source": "stock"}, False),
    ({"source": "bogus"}, True),      # unknown source is conservatively Tier B
])
def test_is_tier_b_source_based(entry, is_b):
    # is_tier_b ignores any `tier` label, keying only on the source.
    smuggled = dict(entry, tier="A1")
    assert rp.is_tier_b(smuggled) is is_b


# ----------------------------------------------------------------- determinism

def _big_manifest():
    return manifest(
        row("tok0311", 128, 128, "room", 1),   # -> 3d (no lib)
        row("tok0100", 64, 64, "hud", 1),       # -> 2
        row("tok0200", 16, 16, "room", 1),      # -> 3a
        row("tok0300", 64, 32, "room", 1),      # -> 3b (warn)
        row("tok0500", 128, 128, "room", 0),    # -> 3e
        row("tok0949", 64, 64, "room", 0),      # override stock
    )


def test_route_is_byte_stable():
    m = _big_manifest()
    ov = {"tok0949": {"source": "stock", "reason": "seams"}}
    a = rp.route(m, ov, None, False)
    b = rp.route(m, ov, None, False)
    assert a == b
    assert rp.serialize_plan(a) == rp.serialize_plan(b)


def test_serialize_strips_transient_and_is_float_free():
    plan = rp.route(_big_manifest(), {}, None, False)
    text = rp.serialize_plan(plan)
    assert "_warnings" not in text and "_note" not in text
    # Byte-stable => reparses to the same structure it renders.
    assert json.loads(text) == json.loads(rp.serialize_plan(json.loads(text)))
    # Float-free: no bare decimal numbers in the emitted JSON.
    reparsed = json.loads(text)

    def no_floats(o):
        if isinstance(o, float):
            return False
        if isinstance(o, dict):
            return all(no_floats(v) for v in o.values())
        if isinstance(o, list):
            return all(no_floats(v) for v in o)
        return True
    assert no_floats(reparsed)


def test_main_run_twice_byte_identical(tmp_path):
    # Full driver determinism: same inputs -> byte-identical plan.json files.
    csv_path = tmp_path / "m.csv"
    csv_path.write_text(
        "token,w,h,fmt,siz,avgRGB,tileable,draw_class\n"
        "tok0311,128,128,0,0,aabbcc,1,room\n"
        "tok0200,16,16,0,0,111111,1,room\n"
        "tok0100,64,64,0,0,222222,1,hud\n"
        "tok0500,128,128,0,0,333333,0,room\n")
    out_a = tmp_path / "a.json"
    out_b = tmp_path / "b.json"
    assert rp.main(["--manifest", str(csv_path), "--level", "dam",
                    "--out", str(out_a)]) == 0
    assert rp.main(["--manifest", str(csv_path), "--level", "dam",
                    "--out", str(out_b)]) == 0
    assert out_a.read_bytes() == out_b.read_bytes()
    plan = json.loads(out_a.read_text())
    assert plan["level"] == "dam" and plan["pack_kind"] == "full"


# --------------------------------------------------- Dam shipped-decision replay

def test_overrides_dam_reproduces_shipped_decisions():
    # The committed curation file must encode the shipped Dam art direction.
    overrides = rp.load_overrides(str(OVERRIDES_DIR / "dam.json"))
    # Real Dam manifest rows (dims/class/tileable are the routing-relevant facts;
    # avgRGB is ROM-derived and irrelevant to routing, so it is omitted here).
    m = manifest(
        row("tok0022", 64, 32, "room", 0),   # ground/gravel hero
        row("tok0949", 64, 64, "room", 0),   # rock (per-quad UV seams)
    )
    plan = rp.route(m, overrides, None, False)
    g = plan["entries"]["tok0022"]
    assert g["source"] == "procedural" and g["preset"] == "gravel"
    assert g["tone"] == {"mode": "match"} and g["tier"] == "B"
    r = plan["entries"]["tok0949"]
    assert r["source"] == "stock" and r["tier"] == "-"
    # tok0022 is tileable=0, so WITHOUT the override the tree would AI-upscale it.
    auto = rp.route(m, {}, None, False)
    assert auto["entries"]["tok0022"]["source"] == "ai_upscale"


def test_overrides_dam_is_clean_committable_json():
    # No ROM data, valid JSON; transient `_`-keys never reach a plan.
    # The load-bearing Dam decisions (roadmap §3: tok0022 procedural, tok0949 stock)
    # must hold; W2.E8.T2 re-curation may add further hero-surface tokens
    # (e.g. tok0291 concrete, tok2228 seam-safe) — assert the key decisions, not a
    # frozen exact set.
    overrides = rp.load_overrides(str(OVERRIDES_DIR / "dam.json"))
    assert {"tok0022", "tok0949"} <= set(overrides)
    assert overrides["tok0022"]["source"] == "procedural"
    assert overrides["tok0949"]["source"] == "stock"
    plan = rp.route(manifest(row("tok0022", 64, 32, "room", 0)), overrides,
                    None, False)
    assert "_note" not in rp.serialize_plan(plan)


# ------------------------------------------------ cross-plan shared-token conflict

def test_conflict_warns_on_contradicting_route():
    # facility routes tok0311 as cc0_import; dam (this plan) routes it seam-safe.
    entries = {"tok0311": {"source": "ai_upscale", "mode": "seam_safe"}}
    others = [("overrides/facility.json",
               {"tok0311": {"source": "cc0_import", "asset": "x"}})]
    warns = rp.conflict_warnings(entries, "dam", others)
    assert len(warns) == 1
    assert "tok0311" in warns[0] and "conflict" in warns[0]


def test_no_conflict_when_routes_agree():
    entries = {"tok0311": {"source": "cc0_import", "asset": "x", "tier": "A1"}}
    others = [("overrides/facility.json",
               {"tok0311": {"source": "cc0_import", "asset": "x"}})]
    assert rp.conflict_warnings(entries, "dam", others) == []


def test_conflict_warnings_are_deterministic():
    entries = {"tok0002": {"source": "ai_upscale"},
               "tok0001": {"source": "lanczos"}}
    others = [("overrides/b.json", {"tok0001": {"source": "stock"}}),
              ("overrides/a.json", {"tok0002": {"source": "stock"}})]
    w1 = rp.conflict_warnings(entries, "dam", others)
    w2 = rp.conflict_warnings(entries, "dam", others)
    assert w1 == w2
    assert "tok0001" in w1[0] and "tok0002" in w1[1]   # sorted by token


# ------------------------------------------- distributable refusal, route side (T2)

def test_distributable_violations_lists_all_tier_b():
    m = manifest(
        row("tok0311", 128, 128, "room", 0),   # ai_upscale -> B
        row("tok0949", 64, 64, "room", 0),      # override stock -> ok
    )
    ov = {"tok0949": {"source": "stock", "reason": "seams"}}
    plan = rp.route(m, ov, None, True)
    assert [t for t, _ in rp.distributable_violations(plan)] == ["tok0311"]


def test_main_distributable_refuses_and_writes_nothing(tmp_path, capsys):
    csv_path = tmp_path / "m.csv"
    csv_path.write_text(
        "token,w,h,fmt,siz,avgRGB,tileable,draw_class\n"
        "tok0107,128,128,0,0,aabbcc,0,room\n")   # -> ai_upscale (Tier B)
    out = tmp_path / "plan.json"
    rc = rp.main(["--manifest", str(csv_path), "--level", "dam",
                  "--distributable", "--out", str(out)])
    assert rc == 1
    err = capsys.readouterr().err
    assert "REFUSED" in err and "tok0107" in err
    assert not out.exists(), "a refused distributable plan must not be written"


def test_main_distributable_passes_when_all_a_tier(tmp_path):
    csv_path = tmp_path / "m.csv"
    csv_path.write_text(
        "token,w,h,fmt,siz,avgRGB,tileable,draw_class\n"
        "tok0949,64,64,0,0,595959,0,room\n")
    ov = tmp_path / "ov.json"
    ov.write_text(json.dumps({"tok0949": {"source": "stock", "reason": "seams"}}))
    out = tmp_path / "plan.json"
    rc = rp.main(["--manifest", str(csv_path), "--overrides", str(ov),
                  "--level", "dam", "--distributable", "--out", str(out)])
    assert rc == 0 and out.exists()
    assert json.loads(out.read_text())["pack_kind"] == "distributable"


# --------------------------------- distributable re-check, build side, source-based (T2)

bp = _load(BUILD_PACK, "build_pack")


def test_build_pack_refuses_smuggled_tier_source_based():
    # A hand-edited plan fakes tier:"A1" on an ai_upscale entry. build_pack.py must
    # STILL flag it (source-based, not label-based) -- the R2 defense-in-depth point.
    plan = {
        "pack_kind": "distributable", "level": "dam",
        "entries": {
            "tok0107": {"source": "ai_upscale", "mode": "whole_image",
                        "tier": "A1"},        # <-- smuggled label
            "tok0949": {"source": "stock", "reason": "seams", "tier": "-"},
        },
    }
    v = bp.plan_tier_b_violations(plan)
    assert [t for t, _ in v] == ["tok0107"], "smuggled A1 must not hide the B token"


def test_build_pack_refuses_smuggled_procedural_match():
    # A tone-matched procedural entry is Tier B even relabeled A1.
    plan = {"entries": {"tok0022": {"source": "procedural",
                                    "tone": {"mode": "match"}, "tier": "A1"}}}
    assert [t for t, _ in bp.plan_tier_b_violations(plan)] == ["tok0022"]


def test_build_pack_distributable_all_a_tier_ok():
    plan = {"entries": {"tok0949": {"source": "stock", "tier": "-"},
                        "tok0311": {"source": "cc0_import", "asset": "x",
                                    "tier": "A2"},
                        "tok0500": {"source": "procedural",
                                    "tone": {"mode": "generic"}, "tier": "A1"}}}
    assert bp.plan_tier_b_violations(plan) == []


# ------------------------ build_pack --distributable fails CLOSED (review fix #1)
# The legacy dump-driven pipeline ignores the plan and AI-upscales every dump
# (Tier B by construction), so --distributable must REFUSE to run it -- a passed
# plan check followed by a legacy build would fail open.

def _a_tier_plan(tmp_path):
    plan = {"pack_kind": "distributable", "level": "dam",
            "entries": {"tok0949": {"source": "stock", "tier": "-"}}}
    p = tmp_path / "plan.json"
    p.write_text(json.dumps(plan))
    return p


def test_build_pack_distributable_refuses_legacy_build(tmp_path, monkeypatch,
                                                       capsys):
    dump = tmp_path / "dump"; dump.mkdir()
    (dump / "settex_0107.png").write_bytes(b"not-a-real-png")
    out = tmp_path / "pack"
    monkeypatch.setattr(sys, "argv",
                        ["build_pack.py", "--plan", str(_a_tier_plan(tmp_path)),
                         "--distributable", "--dump", str(dump),
                         "--out", str(out)])
    with pytest.raises(SystemExit) as ei:
        bp.main()
    msg = str(ei.value.code)
    assert ei.value.code not in (0, None), "must exit non-zero (fail closed)"
    assert "REFUSED" in msg and "plan-driven" in msg
    assert not out.exists(), "a refused distributable build must emit NOTHING"


def test_build_pack_distributable_gate_only_still_passes(tmp_path, monkeypatch,
                                                         capsys):
    monkeypatch.setattr(sys, "argv",
                        ["build_pack.py", "--plan", str(_a_tier_plan(tmp_path)),
                         "--distributable"])
    bp.main()   # no --dump: gate check only, exits 0 by returning
    out = capsys.readouterr().out
    assert "check passed" in out and "gate check only" in out


def test_build_pack_distributable_tier_b_plan_refused_before_any_build(
        tmp_path, monkeypatch):
    plan = {"entries": {"tok0107": {"source": "ai_upscale", "tier": "A1"}}}
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(plan))
    dump = tmp_path / "dump"; dump.mkdir()
    out = tmp_path / "pack"
    monkeypatch.setattr(sys, "argv",
                        ["build_pack.py", "--plan", str(plan_path),
                         "--distributable", "--dump", str(dump),
                         "--out", str(out)])
    with pytest.raises(SystemExit) as ei:
        bp.main()
    assert "REFUSED" in str(ei.value.code) and "tok0107" in str(ei.value.code)
    assert not out.exists()


# --------------------------- non-dict `tone` values, no traceback (review fix #2)
# Hand-edited overrides/plans may carry "tone": "match" (string shorthand for
# {"mode": "match"}) or junk; both enforcement points must handle it cleanly.

@pytest.mark.parametrize("tone,tier", [
    ("match", "B"),        # curator shorthand -> {"mode": "match"}
    ("generic", "A1"),     # shorthand, non-match -> A1
    (["match"], "B"),      # malformed -> conservatively Tier B (never fail open)
    (42, "B"),
    (None, "B"),
    ({}, "A1"),            # dict without mode: unchanged behavior
])
def test_tone_shorthand_and_malformed_never_traceback(tone, tier):
    e = {"source": "procedural", "tone": tone}
    assert rp.compute_tier(e) == tier
    assert rp.is_tier_b(e) is (tier == "B")
    assert bp._entry_is_tier_b(e) is (tier == "B")


# ------------------- overrides for tokens absent from the manifest (review fix #3)

def test_route_warns_on_override_token_missing_from_manifest():
    m = manifest(row("tok0311", 128, 128, "room", 0))
    ov = {"tok0311": {"source": "stock"},          # in manifest: applied
          "tok0777": {"source": "stock"}}          # NOT in manifest: dropped
    plan = rp.route(m, ov, None, False)
    assert "tok0777" not in plan["entries"]
    assert plan["entries"]["tok0311"]["source"] == "stock"
    warns = [w for w in plan["_warnings"] if "tok0777" in w]
    assert len(warns) == 1 and "WARN" in warns[0] and "dropped" in warns[0]


def test_main_prints_dropped_override_warning_to_stderr(tmp_path, capsys):
    csv_path = tmp_path / "m.csv"
    csv_path.write_text(
        "token,w,h,fmt,siz,avgRGB,tileable,draw_class\n"
        "tok0311,128,128,0,0,aabbcc,0,room\n")
    ov = tmp_path / "ov.json"
    ov.write_text(json.dumps({"tok0777": {"source": "stock"}}))
    out = tmp_path / "plan.json"
    assert rp.main(["--manifest", str(csv_path), "--overrides", str(ov),
                    "--level", "dam", "--out", str(out)]) == 0
    err = capsys.readouterr().err
    assert "WARN" in err and "tok0777" in err
    assert "tok0777" not in out.read_text()


# --------------------------- shared normalize_token_key, no drift (review fix #4)

def test_normalize_token_key_is_the_shared_build_pack_implementation():
    # route_pack imports it from build_pack -- ONE implementation, no divergence.
    assert rp.normalize_token_key.__module__ == "build_pack"
    for raw in ("tok12", "settex_12", "12", " tok12 ", "tok12 ", 12):
        assert rp.normalize_token_key(raw) == "tok0012", raw
        assert bp.normalize_token_key(raw) == "tok0012", raw
    for raw in (None, "nope", ""):
        assert rp.normalize_token_key(raw) is None
        assert bp.normalize_token_key(raw) is None


# ------------------- is_tier_b == (compute_tier == B), single table (review fix #5)

_REPRESENTATIVE_ENTRIES = [
    {"source": "ai_upscale", "mode": "whole_image"},
    {"source": "lanczos"},
    {"source": "procedural", "tone": {"mode": "match"}},
    {"source": "procedural", "tone": {"mode": "generic"}},
    {"source": "procedural", "tone": "match"},
    {"source": "procedural", "tone": 42},
    {"source": "procedural"},
    {"source": "cc0_import", "asset": "x", "tier": "A2"},
    {"source": "original"},
    {"source": "stock"},
    {"source": "bogus"},
    {},
]


def test_is_tier_b_delegates_to_compute_tier():
    for e in _REPRESENTATIVE_ENTRIES:
        assert rp.is_tier_b(e) is (rp.compute_tier(e) == "B"), e


def test_cheapest_alternative_reflects_the_entry():
    # A tone-matched procedural entry's cheapest A-tier route is the SAME preset
    # with a generic tone; AI-upscale/lanczos entries get the generic advice.
    alt_proc = rp.cheapest_alternative({"source": "procedural",
                                        "tone": {"mode": "match"}})
    assert "generic" in alt_proc and "stock" not in alt_proc
    alt_ai = rp.cheapest_alternative({"source": "ai_upscale"})
    assert "stock" in alt_ai
    # And format_refusal carries the per-entry advice through.
    msg = rp.format_refusal([
        ("tok0022", {"source": "procedural", "tone": {"mode": "match"}}),
        ("tok0107", {"source": "ai_upscale"})])
    assert alt_proc in msg and alt_ai in msg


# ---------------- route_pack vs build_pack Tier-B predicate parity (review fix #6)
# The tier rule is DELIBERATELY duplicated (independent enforcement points), so
# this parity matrix is what keeps the two copies from drifting apart.

def test_tier_b_predicates_agree_across_tools():
    sources = ["ai_upscale", "lanczos", "procedural", "cc0_import", "original",
               "stock", "bogus", None]
    tones = ["_ABSENT_", {"mode": "match"}, {"mode": "generic"}, {},
             "match", "generic", ["match"], 42, None]
    for src in sources:
        for tone in tones:
            e = {"tier": "A1"}   # smuggled label: must be ignored by BOTH
            if src is not None:
                e["source"] = src
            if tone != "_ABSENT_":
                e["tone"] = tone
            assert rp.is_tier_b(e) is bp._entry_is_tier_b(e), e
