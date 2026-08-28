"""W2.E7.T1 -- validate_pack.py offline structural QA gate.

Exercises the real validator (imported from tools/texpack/validate_pack.py) against
synthetic packs built entirely IN-TEST: fake manifests, fake PNGs, fake `.alpha.pgm`
dumps. Nothing here is tracked and nothing is ROM-derived (R2) -- the same posture as
the sibling test_tileable_parity.py / test_route_pack.py.

Acceptance (docs/design/remaster-aaa/02-hd-asset-pipeline.md §5, W2.E7.T1 row): each seeded
BAD pack produces a specific NAMED FAIL, and a valid Dam-shaped pack PASSES.
  * 5000px asset          -> FAIL check=dims-oversize
  * tok22.png             -> FAIL check=bad-filename
  * opaque grate (--dump) -> FAIL check=alpha-dropped
  * non-wrapping tile     -> FAIL check=seam-broken
  * valid Dam pack        -> PASS (exit 0)
"""
import importlib.util
import subprocess
import sys
from pathlib import Path

import pytest

pytest.importorskip("PIL", reason="Pillow required (tools/texpack/requirements.txt)")
from PIL import Image  # noqa: E402

TESTS_DIR = Path(__file__).resolve().parent
VALIDATE_PACK = TESTS_DIR.parent / "validate_pack.py"

MANIFEST_HEADER = "token,w,h,fmt,siz,avgRGB,tileable,draw_class"


def _load_validate_pack():
    spec = importlib.util.spec_from_file_location("texpack_validate_pack", VALIDATE_PACK)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


vp = _load_validate_pack()


# ---- synthetic image factories (generated in-test, never tracked) -----------

def _solid_rgba(w, h, v=120, a=255):
    return Image.new("RGBA", (w, h), (v, v, v, a))


def _hgrad_rgba(w, h):
    """Left column 0 -> right column 255: opposite edges differ hugely, so the
    is_tileable edge metric returns False (a tile that does NOT wrap)."""
    im = Image.new("RGBA", (w, h))
    im.putdata([(int(x * 255 / (w - 1)),) * 3 + (255,)
                for _y in range(h) for x in range(w)])
    return im


def _grate_alpha_pgm(w, h):
    """An 'L' alpha channel with a grid of holes (0) -> meaningful transparency,
    the way a settex .alpha.pgm cutout looks in the dump."""
    im = Image.new("L", (w, h), 255)
    im.putdata([0 if ((x // 4) % 2 == 0 and (y // 4) % 2 == 0) else 255
                for y in range(h) for x in range(w)])
    return im


def _grate_rgba_with_alpha(w, h):
    """The correct HD grate: RGB solid, alpha carrying the same hole pattern."""
    a = _grate_alpha_pgm(w, h)
    im = Image.new("RGBA", (w, h), (90, 90, 90, 255))
    im.putalpha(a)
    return im


# ---- pack / manifest / dump builders ----------------------------------------

def _write_pack(root, images):
    """images: {basename: PIL image}. Writes <root>/textures/<name>."""
    tex = Path(root) / "textures"
    tex.mkdir(parents=True, exist_ok=True)
    for name, im in images.items():
        im.save(tex / name)
    return str(root)


def _write_manifest(path, rows):
    """rows: iterable of (token,w,h,fmt,siz,avgRGB,tileable,draw_class) tuples."""
    lines = [MANIFEST_HEADER]
    lines += [",".join(str(c) for c in r) for r in rows]
    Path(path).write_text("\n".join(lines) + "\n")
    return str(path)


def _write_dump(dump_dir, alphas):
    """alphas: {token_int: PIL 'L' image}. Writes ge007_settex_####.alpha.pgm."""
    d = Path(dump_dir)
    d.mkdir(parents=True, exist_ok=True)
    for tok, a in alphas.items():
        a.save(d / ("ge007_settex_%04d.alpha.pgm" % tok))
    return str(d)


def _make_good_pack(tmp_path):
    """A valid Dam-shaped pack: a wrapping tile, a whole-image asset, and a grate
    whose alpha is correctly preserved (verified against the dump)."""
    pack = tmp_path / "good_pack"
    _write_pack(pack, {
        "tok0001.png": _solid_rgba(128, 128, v=100),        # wraps trivially
        "tok0002.png": _hgrad_rgba(128, 128),               # whole-image (not tileable)
        "tok0005.png": _grate_rgba_with_alpha(128, 128),    # cutout preserved
    })
    manifest = _write_manifest(tmp_path / "good.csv", [
        ("tok0001", 32, 32, 0, 2, "0x808080", 1, "room"),    # tileable
        ("tok0002", 32, 32, 0, 2, "0x404040", 0, "weapon"),  # not tileable
        ("tok0005", 32, 32, 3, 0, "0x5a5a5a", 0, "chrprop"), # cutout
    ])
    dump = _write_dump(tmp_path / "good_dump", {
        5: _grate_alpha_pgm(32, 32),                         # original had the holes
    })
    return str(pack), manifest, dump


# ------------------------------------------------------------------- tests ----

def test_good_pack_passes(tmp_path):
    pack, manifest, dump = _make_good_pack(tmp_path)
    rep = vp.validate_pack(pack, manifest, dump)
    assert rep.ok, f"good pack should PASS; failures={rep.failures}"
    assert rep.failures == []


def test_5000px_asset_fails_dims(tmp_path):
    pack = tmp_path / "big_pack"
    # 5000px wide asset -> exceeds the 4096 cap both backends enforce.
    _write_pack(pack, {"tok0001.png": _solid_rgba(5000, 16)})
    manifest = _write_manifest(tmp_path / "big.csv", [])
    rep = vp.validate_pack(str(pack), manifest)
    assert rep.has_fail("dims-oversize"), rep.findings
    assert not rep.ok


def test_bad_filename_tok22_fails(tmp_path):
    pack = tmp_path / "name_pack"
    # tok22.png -> only 2 digits, not tok\d{4}.png
    _write_pack(pack, {"tok22.png": _solid_rgba(64, 64)})
    manifest = _write_manifest(tmp_path / "name.csv", [])
    rep = vp.validate_pack(str(pack), manifest)
    assert rep.has_fail("bad-filename"), rep.findings
    assert not rep.ok


def test_opaque_grate_fails_alpha_dropped(tmp_path):
    pack = tmp_path / "grate_pack"
    # HD grate rendered OPAQUE (alpha all 255) while the dump proves the original
    # had a cutout -> dropped transparency. Needs --dump to detect.
    _write_pack(pack, {"tok0005.png": _solid_rgba(128, 128)})
    manifest = _write_manifest(tmp_path / "grate.csv", [
        ("tok0005", 32, 32, 3, 0, "0x5a5a5a", 0, "chrprop"),
    ])
    dump = _write_dump(tmp_path / "grate_dump", {5: _grate_alpha_pgm(32, 32)})
    rep = vp.validate_pack(str(pack), manifest, dump)
    assert rep.has_fail("alpha-dropped"), rep.findings
    assert not rep.ok


def test_opaque_grate_needs_dump_to_detect(tmp_path):
    """Without --dump the alpha truth is unknown (manifest has no alpha column):
    the SAME opaque grate is NOT flagged -- proving --dump is what catches it."""
    pack = tmp_path / "grate_pack2"
    _write_pack(pack, {"tok0005.png": _solid_rgba(128, 128)})
    manifest = _write_manifest(tmp_path / "grate2.csv", [
        ("tok0005", 32, 32, 3, 0, "0x5a5a5a", 0, "chrprop"),
    ])
    rep = vp.validate_pack(str(pack), manifest, dump_dir=None)
    assert not rep.has_fail("alpha-dropped")
    assert rep.ok


def test_nonwrapping_tile_fails_seam(tmp_path):
    pack = tmp_path / "seam_pack"
    # Manifest marks tok0001 tileable, but the HD output is a gradient that does
    # NOT wrap -> seam self-check FAIL.
    _write_pack(pack, {"tok0001.png": _hgrad_rgba(128, 128)})
    manifest = _write_manifest(tmp_path / "seam.csv", [
        ("tok0001", 32, 32, 0, 2, "0x808080", 1, "room"),
    ])
    rep = vp.validate_pack(str(pack), manifest)
    assert rep.has_fail("seam-broken"), rep.findings
    assert not rep.ok


def test_token_out_of_range_fails(tmp_path):
    pack = tmp_path / "range_pack"
    # tok4096 -> shape-valid 4-digit token but >= TEXPACK_MAX_TOKENS (0..4095).
    _write_pack(pack, {"tok4096.png": _solid_rgba(64, 64)})
    manifest = _write_manifest(tmp_path / "range.csv", [])
    rep = vp.validate_pack(str(pack), manifest)
    assert rep.has_fail("token-range"), rep.findings
    assert not rep.ok


def test_over_budget_fails(tmp_path):
    pack = tmp_path / "budget_pack"
    # One 2048^2 RGBA texture = 16 MB; a 1 MB budget must FAIL.
    _write_pack(pack, {"tok0001.png": _solid_rgba(2048, 2048)})
    manifest = _write_manifest(tmp_path / "budget.csv", [])
    rep = vp.validate_pack(str(pack), manifest, budget_mb=1)
    assert rep.has_fail("budget"), rep.findings
    assert not rep.ok


def test_missing_textures_dir_fails(tmp_path):
    pack = tmp_path / "no_tex"
    pack.mkdir()
    manifest = _write_manifest(tmp_path / "empty.csv", [])
    rep = vp.validate_pack(str(pack), manifest)
    assert rep.has_fail("no-textures-dir"), rep.findings


def test_valid_sidecars_pass(tmp_path):
    """`_n`/`_r` material sidecars are accepted filenames (not bad-filename)."""
    pack = tmp_path / "sidecar_pack"
    _write_pack(pack, {
        "tok0001.png": _solid_rgba(128, 128, v=100),
        "tok0001_n.png": _solid_rgba(128, 128, v=128),
        "tok0001_r.png": _solid_rgba(128, 128, v=200),
    })
    manifest = _write_manifest(tmp_path / "sidecar.csv", [
        ("tok0001", 32, 32, 0, 2, "0x808080", 0, "room"),
    ])
    rep = vp.validate_pack(str(pack), manifest)
    assert not rep.has_fail("bad-filename"), rep.findings
    assert rep.ok, rep.failures


# ---- CLI contract (exit codes + machine-greppable NAMED FAIL lines) ----------

def _run_cli(args):
    return subprocess.run([sys.executable, str(VALIDATE_PACK), *args],
                          capture_output=True, text=True)


def test_cli_good_pack_exit0_and_pass_line(tmp_path):
    pack, manifest, dump = _make_good_pack(tmp_path)
    out = _run_cli(["--pack", pack, "--manifest", manifest, "--dump", dump])
    assert out.returncode == 0, out.stdout + out.stderr
    assert "VALIDATE_PACK PASS" in out.stdout


def test_cli_bad_pack_exit1_and_named_fail_line(tmp_path):
    pack = tmp_path / "cli_bad"
    _write_pack(pack, {"tok0001.png": _solid_rgba(5000, 16)})
    manifest = _write_manifest(tmp_path / "cli_bad.csv", [])
    out = _run_cli(["--pack", str(pack), "--manifest", manifest])
    assert out.returncode == 1, out.stdout + out.stderr
    assert "VALIDATE_PACK FAIL check=dims-oversize" in out.stdout
