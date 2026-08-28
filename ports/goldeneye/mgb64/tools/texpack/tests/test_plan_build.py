"""W2.E2.T3 -- build_pack.py plan-driven build + per-batch failure isolation.

Two acceptance legs, both proven with SYNTHETIC fixtures + a STUB upscaler we
control (no ROM data, no Real-ESRGAN required -- see doc 02 §4.5 items 3-4):

  Leg 1 (batch failure isolation): a batch whose upscaler exits non-zero must NOT
    abort the whole pack. The OTHER batches finish, the build exits 1, and the
    summary NAMES the failed tokens. Proven on BOTH the plan path and the legacy
    --route path (they share `_run_ai_batches`).

  Leg 2 (--plan build equivalence): `build_pack.py --plan plan.json` produces the
    SAME pack (sha256 per file) as the legacy dump-driven build for every token
    the plan routes to ai_upscale / lanczos. The plan comes from the real
    route_pack.py router; both builds run the SAME synthetic dump through the SAME
    stub upscaler, so identical bytes prove per-token dispatch equivalence.

The stub upscaler is a deterministic feed-forward stand-in for
realesrgan-ncnn-vulkan (README: "same input always yields the same output"): its
output is a pure function of (input pixels, scale, model name), and it honors
STUB_FAIL_MODELS to simulate a killed/failed batch. It is written into pytest's
tmp dir at run time -- never tracked (R2).

Contamination (R2): every fixture (images, dump .ppm/.pgm, packs) lives under
pytest tmp_path, outside the repo; the global .gitignore also covers *.png /
*.ppm / *.pgm. Nothing here is ROM-derived.
"""
import hashlib
import importlib.util
import json
import os
import sys
from pathlib import Path

import pytest

pytest.importorskip("PIL", reason="Pillow required (tools/texpack/requirements.txt)")
from PIL import Image  # noqa: E402

TESTS_DIR = Path(__file__).resolve().parent
BUILD_PACK = TESTS_DIR.parent / "build_pack.py"
ROUTE_PACK = TESTS_DIR.parent / "route_pack.py"


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    # route_pack imports `from build_pack import ...`; make the dir importable.
    sys.path.insert(0, str(path.parent))
    spec.loader.exec_module(mod)
    return mod


bp = _load(BUILD_PACK, "build_pack")
rp = _load(ROUTE_PACK, "route_pack")


# --------------------------------------------------------------- stub upscaler

# Deterministic stand-in for realesrgan-ncnn-vulkan (ncnn directory mode). Same
# input -> same output (feed-forward), output depends on (pixels, scale, model)
# EXACTLY as the real upscaler does, so a model mismatch between the two build
# paths WOULD change the bytes and fail the equivalence sha check. STUB_FAIL_MODELS
# (comma-separated model names) makes the named batch exit non-zero WITHOUT writing
# output -- the "killed mid-batch" case.
_STUB_BODY = r'''
import argparse, glob, hashlib, os, sys
from PIL import Image

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", required=True)
    ap.add_argument("-o", required=True)
    ap.add_argument("-s", type=int, default=4)
    ap.add_argument("-n", default="")
    ap.add_argument("-m", default="")
    ap.add_argument("-f", default="png")
    a = ap.parse_args()
    fail = {x for x in os.environ.get("STUB_FAIL_MODELS", "").split(",") if x}
    if a.n in fail:
        sys.stderr.write("stub-upscaler: simulated failure on model %s\n" % a.n)
        return 1
    os.makedirs(a.o, exist_ok=True)
    delta = hashlib.sha256(a.n.encode()).digest()[0] % 13
    for src in sorted(glob.glob(os.path.join(a.i, "*.png"))):
        im = Image.open(src).convert("RGBA")
        w, h = im.size
        up = im.resize((w * a.s, h * a.s), Image.NEAREST)
        if delta:
            up = up.point(lambda p: (p + delta) % 256)
        up.save(os.path.join(a.o, os.path.basename(src)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
'''


def _write_stub(dir_path):
    """Write the stub upscaler as a directly-executable script (shebang = the
    running interpreter, so PIL is guaranteed present). Returns its path."""
    stub = Path(dir_path) / "stub_esrgan.py"
    stub.write_text("#!" + sys.executable + "\n" + _STUB_BODY)
    stub.chmod(0o755)
    return str(stub)


# --------------------------------------------------------------- image fixtures

def _img(w, h, fn):
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = fn(x, y)
    return im


def _forced_wrap(w, h, alpha=255):
    """Tileable: pseudo-random interior, but col w-1 := col 0 and row h-1 := row 0
    so both opposite-edge means are 0 -> is_tileable == True (min dim >= 8)."""
    im = _img(w, h, lambda x, y: ((x * 37 + y * 91) % 200 + 20,
                                  (x * 53 + y * 17) % 200 + 20,
                                  (x * 11 + y * 73) % 200 + 20, alpha))
    px = im.load()
    for y in range(h):
        r, g, b, a = px[0, y]
        px[w - 1, y] = (r, g, b, a)
    for x in range(w):
        r, g, b, a = px[x, 0]
        px[x, h - 1] = (r, g, b, a)
    return im


def _hgrad(w, h, alpha_grad=False):
    """Horizontal gradient -> opposite columns differ -> is_tileable == False."""
    def fn(x, y):
        v = int(x * 255 / (w - 1))
        a = int(y * 255 / (h - 1)) if alpha_grad else 255
        return (v, (v + 64) % 256, (v + 128) % 256, a)
    return _img(w, h, fn)


def _write_dump(dump_dir, tok, im):
    """Write one texture to the dump in the engine's format: RGB to
    <tok>.rgba.ppm and the alpha channel separately to <tok>.alpha.pgm (the exact
    pair load_rgba re-attaches, build_pack.py:74-89)."""
    os.makedirs(dump_dir, exist_ok=True)
    base = os.path.join(dump_dir, tok)
    im.convert("RGB").save(base + ".rgba.ppm")
    im.getchannel("A").save(base + ".alpha.pgm")


def _sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _run_main(monkeypatch, argv):
    """Invoke build_pack.main() with the given argv; return the SystemExit code
    (0 if main returned normally)."""
    monkeypatch.setattr(sys, "argv", ["build_pack.py"] + argv)
    try:
        bp.main()
        return 0
    except SystemExit as ei:
        return ei.code if isinstance(ei.code, int) else (0 if ei.code is None else 1)


# ================================================================ LEG 2: equivalence

def test_plan_build_matches_legacy_for_ai_and_lanczos_tokens(tmp_path, monkeypatch):
    """`--plan` build == legacy dump build (sha256 per file) for every token the
    router routes to ai_upscale / lanczos. Covers seam_safe, whole_image (room and
    non-room), and lanczos, plus a stock-skip and a deferred procedural entry."""
    dump = tmp_path / "dump"
    stub = _write_stub(tmp_path)
    models = tmp_path / "models"; models.mkdir()   # stub ignores contents

    # token -> (image, draw_class). Chosen so the router's mode decision and the
    # legacy is_tileable decision AGREE for every ai token (see module docstring):
    #  - tileable ROOM (>64)   -> seam_safe   (legacy tiles it too: is_tileable=1)
    #  - non-tileable           -> whole_image (legacy no-tile: is_tileable=0)
    #  - tiny ROOM (<=16)       -> lanczos     (legacy tiny-Lanczos too)
    fixtures = {
        "tok0010": (_forced_wrap(128, 128), "room"),           # -> seam_safe
        "tok0020": (_hgrad(128, 128, alpha_grad=True), "room"),  # -> whole_image
        "tok0030": (_hgrad(96, 96), "hud"),                    # -> whole_image (branch 2)
        "tok0040": (_hgrad(16, 16), "room"),                   # -> lanczos (branch 3a)
        "tok0050": (_forced_wrap(64, 64), "room"),             # -> stock (branch 3b)
        "tok0060": (_forced_wrap(128, 128), "room"),           # -> procedural (override)
    }
    for tok, (im, _cls) in fixtures.items():
        _write_dump(str(dump), tok, im)

    # A manifest whose `tileable` column is the REAL is_tileable verdict, so the
    # router and the legacy path can never disagree about seamlessness.
    tileable = {tok: int(bp.is_tileable(im)) for tok, (im, _c) in fixtures.items()}
    assert tileable == {"tok0010": 1, "tok0020": 0, "tok0030": 0,
                        "tok0040": 0, "tok0050": 1, "tok0060": 1}, tileable
    csv_path = tmp_path / "m.csv"
    lines = ["token,w,h,fmt,siz,avgRGB,tileable,draw_class"]
    for tok, (im, cls) in fixtures.items():
        w, h = im.size
        lines.append(f"{tok},{w},{h},0,0,000000,{tileable[tok]},{cls}")
    csv_path.write_text("\n".join(lines) + "\n")

    overrides = tmp_path / "ov.json"
    overrides.write_text(json.dumps({
        "tok0060": {"source": "procedural", "preset": "gravel",
                    "tone": {"mode": "match"}},
    }))

    plan_path = tmp_path / "plan.json"
    assert rp.main(["--manifest", str(csv_path), "--overrides", str(overrides),
                    "--level", "dam", "--out", str(plan_path)]) == 0
    plan = json.loads(plan_path.read_text())
    ent = plan["entries"]
    # The router produced exactly the routes this test is built around.
    assert ent["tok0010"]["source"] == "ai_upscale" and ent["tok0010"]["mode"] == "seam_safe"
    assert ent["tok0020"]["source"] == "ai_upscale" and ent["tok0020"]["mode"] == "whole_image"
    assert ent["tok0030"]["source"] == "ai_upscale" and ent["tok0030"]["mode"] == "whole_image"
    assert ent["tok0040"]["source"] == "lanczos"
    assert ent["tok0050"]["source"] == "stock"
    assert ent["tok0060"]["source"] == "procedural"
    # Model equivalence: every ai entry carries the same model the legacy default
    # uses, so the two builds invoke the stub with an identical model per token.
    for tok in ("tok0010", "tok0020", "tok0030"):
        assert ent[tok]["model"] == "realesrgan-x4plus"

    # Legacy dump-driven build (no --route -> default model for every token).
    out_legacy = tmp_path / "legacy"
    rc = _run_main(monkeypatch, ["--dump", str(dump), "--out", str(out_legacy),
                                 "--realesrgan", stub, "--models", str(models)])
    assert rc == 0, "legacy build should succeed"

    # Plan-driven build (same dump, same stub).
    out_plan = tmp_path / "plan_pack"
    rc = _run_main(monkeypatch, ["--plan", str(plan_path), "--dump", str(dump),
                                 "--out", str(out_plan), "--realesrgan", stub,
                                 "--models", str(models)])
    assert rc == 0, "plan build should succeed (procedural is deferred, not fatal)"

    lt = out_legacy / "textures"
    pt = out_plan / "textures"
    # Byte-identical for every ai_upscale / lanczos token.
    ai_lanczos = ["tok0010", "tok0020", "tok0030", "tok0040"]
    for tok in ai_lanczos:
        lf, pf = lt / f"{tok}.png", pt / f"{tok}.png"
        assert lf.exists() and pf.exists(), f"{tok}: missing in a pack"
        assert _sha(lf) == _sha(pf), f"{tok}: plan build differs from legacy build"

    # The plan build correctly SKIPPED the stock token and DEFERRED procedural.
    assert not (pt / "tok0050.png").exists(), "stock token must not be built"
    assert not (pt / "tok0060.png").exists(), "procedural token is deferred (E5)"
    # Sanity: the seam-safe crop actually ran (output is native size x scale).
    assert Image.open(pt / "tok0010.png").size == (512, 512)


def test_plan_lanczos_is_bit_exact_and_never_calls_the_upscaler(tmp_path,
                                                                 monkeypatch):
    """A lanczos-routed token is a deterministic PIL resize -- it must build with
    NO upscaler at all (a non-existent --realesrgan is fine when no ai tokens)."""
    dump = tmp_path / "dump"
    im = _hgrad(16, 16)
    _write_dump(str(dump), "tok0040", im)
    plan = tmp_path / "plan.json"
    plan.write_text(json.dumps({"pack_kind": "full", "level": "t",
                                "entries": {"tok0040": {"source": "lanczos"}}}))
    out = tmp_path / "pack"
    rc = _run_main(monkeypatch, ["--plan", str(plan), "--dump", str(dump),
                                 "--out", str(out),
                                 "--realesrgan", "/nonexistent/esrgan"])
    assert rc == 0, "lanczos-only plan must not require the upscaler"
    got = out / "textures" / "tok0040.png"
    assert got.exists()
    # Bit-exact against a direct PIL Lanczos of the same loaded source.
    ref = tmp_path / "ref.png"
    bp.load_rgba(str(dump / "tok0040.rgba.ppm")).resize(
        (64, 64), Image.LANCZOS).save(ref)
    assert _sha(got) == _sha(ref)


# ============================================================ LEG 1: failure isolation

def test_batch_failure_isolation_plan_path(tmp_path, monkeypatch, capsys):
    """Killing the upscaler on ONE model batch: the other batch still builds, the
    build exits 1, and the summary NAMES the failed token. The failing batch sorts
    FIRST, so a successful later batch proves the build CONTINUED past the failure."""
    dump = tmp_path / "dump"
    stub = _write_stub(tmp_path)
    models = tmp_path / "models"; models.mkdir()
    for tok in ("tok0100", "tok0101", "tok0200"):
        _write_dump(str(dump), tok, _hgrad(32, 32))

    # Two model batches; the failing one ("esrgan_fail") sorts before "esrgan_ok".
    plan = tmp_path / "plan.json"
    plan.write_text(json.dumps({"pack_kind": "full", "level": "t", "entries": {
        "tok0200": {"source": "ai_upscale", "mode": "whole_image",
                    "model": "esrgan_fail"},
        "tok0100": {"source": "ai_upscale", "mode": "whole_image",
                    "model": "esrgan_ok"},
        "tok0101": {"source": "ai_upscale", "mode": "whole_image",
                    "model": "esrgan_ok"},
    }}))
    out = tmp_path / "pack"
    monkeypatch.setenv("STUB_FAIL_MODELS", "esrgan_fail")
    rc = _run_main(monkeypatch, ["--plan", str(plan), "--dump", str(dump),
                                 "--out", str(out), "--realesrgan", stub,
                                 "--models", str(models)])
    assert rc == 1, "a mid-batch failure must exit non-zero"

    tex = out / "textures"
    # The OTHER batch's tokens were built despite the earlier failure.
    assert (tex / "tok0100.png").exists() and (tex / "tok0101.png").exists()
    # The failed batch's token was NOT built.
    assert not (tex / "tok0200.png").exists()

    err = capsys.readouterr().err
    assert "tok0200" in err, "the summary must name the failed token"
    assert "esrgan_fail" in err, "the failed batch/model should be reported"
    # The successful tokens must not be listed as failed.
    summary = err.split("BUILD INCOMPLETE", 1)[1]
    assert "tok0100" not in summary and "tok0101" not in summary


def test_batch_failure_isolation_legacy_route_path(tmp_path, monkeypatch, capsys):
    """The SAME isolation on the legacy --route path (it shares _run_ai_batches).
    A hud token (anime model) and a room token (photo model) form two batches; the
    anime batch is killed, the room batch still builds, exit 1 names the hud token."""
    dump = tmp_path / "dump"
    stub = _write_stub(tmp_path)
    models = tmp_path / "models"; models.mkdir()
    # Non-tileable so both go whole-image; > TINY so neither is auto-Lanczos.
    _write_dump(str(dump), "tok0700", _hgrad(48, 48))   # room  -> realesrgan-x4plus
    _write_dump(str(dump), "tok0800", _hgrad(48, 48))   # hud   -> realesrgan-x4plus-anime

    # A manifest that routes by draw_class (CLASS_MODEL: hud -> anime).
    manifest = dump / "ge007.texmanifest.csv"
    manifest.write_text(
        "token,w,h,fmt,siz,avgRGB,tileable,draw_class\n"
        "tok0700,48,48,0,0,000000,0,room\n"
        "tok0800,48,48,0,0,000000,0,hud\n")
    out = tmp_path / "pack"
    monkeypatch.setenv("STUB_FAIL_MODELS", "realesrgan-x4plus-anime")
    rc = _run_main(monkeypatch, ["--dump", str(dump), "--out", str(out), "--route",
                                 "--realesrgan", stub, "--models", str(models)])
    assert rc == 1
    tex = out / "textures"
    assert (tex / "tok0700.png").exists(), "the photo batch must still build"
    assert not (tex / "tok0800.png").exists(), "the killed anime batch is not built"
    err = capsys.readouterr().err
    assert "tok0800" in err and "BUILD INCOMPLETE" in err
    assert "tok0700" not in err.split("BUILD INCOMPLETE", 1)[1]


def test_plan_missing_dump_source_is_a_hard_failure(tmp_path, monkeypatch, capsys):
    """An ai/lanczos entry with no matching dump texture is a real failure: named,
    exit 1 (not silently dropped)."""
    dump = tmp_path / "dump"
    _write_dump(str(dump), "tok0100", _hgrad(32, 32))   # present
    stub = _write_stub(tmp_path)
    plan = tmp_path / "plan.json"
    plan.write_text(json.dumps({"pack_kind": "full", "level": "t", "entries": {
        "tok0100": {"source": "ai_upscale", "mode": "whole_image"},
        "tok0999": {"source": "ai_upscale", "mode": "whole_image"},  # NOT in dump
    }}))
    out = tmp_path / "pack"
    rc = _run_main(monkeypatch, ["--plan", str(plan), "--dump", str(dump),
                                 "--out", str(out), "--realesrgan", stub])
    assert rc == 1
    err = capsys.readouterr().err
    assert "tok0999" in err
    assert (out / "textures" / "tok0100.png").exists(), "present token still builds"


def test_plan_stock_only_builds_nothing_and_succeeds(tmp_path, monkeypatch):
    """A plan of only stock entries needs no dump and no upscaler; it exits 0 and
    emits no textures (the game keeps its own art)."""
    plan = tmp_path / "plan.json"
    plan.write_text(json.dumps({"pack_kind": "full", "level": "t", "entries": {
        "tok0949": {"source": "stock", "reason": "seams"},
    }}))
    out = tmp_path / "pack"
    rc = _run_main(monkeypatch, ["--plan", str(plan), "--out", str(out)])
    assert rc == 0
    assert list((out / "textures").glob("*.png")) == []


# ============================================ RED-TEAM: --distributable stays fail-closed

def test_distributable_plan_build_still_refuses_tier_b(tmp_path, monkeypatch):
    """Adding the plan build must NOT weaken E3.T2's Tier-B refusal. A plan with an
    ai_upscale (Tier B) entry -- even with a smuggled tier:'A1' label -- must be
    REFUSED by --distributable, source-based, before any build work."""
    dump = tmp_path / "dump"
    _write_dump(str(dump), "tok0107", _hgrad(64, 64))
    plan = tmp_path / "plan.json"
    plan.write_text(json.dumps({"pack_kind": "distributable", "level": "t",
                                "entries": {
        "tok0107": {"source": "ai_upscale", "mode": "whole_image",
                    "tier": "A1"},   # <-- smuggled label; must be ignored
    }}))
    out = tmp_path / "pack"
    monkeypatch.setattr(sys, "argv", ["build_pack.py", "--plan", str(plan),
                                      "--distributable", "--dump", str(dump),
                                      "--out", str(out)])
    with pytest.raises(SystemExit) as ei:
        bp.main()
    assert ei.value.code not in (0, None), "must fail closed (non-zero)"
    assert "REFUSED" in str(ei.value.code) and "tok0107" in str(ei.value.code)
    assert not out.exists(), "a refused distributable build must emit NOTHING"


def test_distributable_a_tier_plan_with_dump_still_refuses_the_build(tmp_path,
                                                                     monkeypatch):
    """Even an ALL-A-tier plan: --distributable + --dump must still refuse (the
    distributable A-tier build is W2.E4/E5, not this task). The plan build path is
    only reached WITHOUT --distributable."""
    plan = tmp_path / "plan.json"
    plan.write_text(json.dumps({"pack_kind": "distributable", "level": "t",
                                "entries": {
        "tok0949": {"source": "stock", "tier": "-"},
    }}))
    dump = tmp_path / "dump"; dump.mkdir()
    out = tmp_path / "pack"
    monkeypatch.setattr(sys, "argv", ["build_pack.py", "--plan", str(plan),
                                      "--distributable", "--dump", str(dump),
                                      "--out", str(out)])
    with pytest.raises(SystemExit) as ei:
        bp.main()
    assert ei.value.code not in (0, None)
    assert "REFUSED" in str(ei.value.code) and "plan-driven" in str(ei.value.code)
    assert not out.exists()
