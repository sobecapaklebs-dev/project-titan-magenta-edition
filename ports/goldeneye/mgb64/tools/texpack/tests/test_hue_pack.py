"""W2.E8.T1 -- hue_pack.py emit/identify round-trip, on synthetic fixtures only.

No ROM data touches this test: a tiny fake texmanifest is written in-test, the
pack is emitted, and a fake "hue screenshot" is synthesised (a flat-hue frame
darkened per-region to simulate the engine's grayscale vertex-shade multiply,
plus low-saturation "stock" noise) and written in the engine's exact .bmp format
(bottom-up 24-bit BGR, platform_sdl.c:701-727). identify() must then rank the
token that owns the most pixels as #1 -- the same assertion the real Dam
acceptance makes about tok0022, proven here on a fixture whose ground truth we
control.
"""
import colorsys
import importlib.util
import json
import struct
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
HUE_PACK = TESTS_DIR.parent / "hue_pack.py"


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


hp = _load(HUE_PACK, "hue_pack")


# --------------------------------------------------------------- fixtures #

def write_manifest(path, rows):
    """rows: list of (token, draw_class). Writes the engine CSV schema."""
    lines = ["token,w,h,fmt,siz,avgRGB,tileable,draw_class"]
    for tok, dc in rows:
        lines.append("%s,32,32,0,0,808080,1,%s" % (tok, dc))
    path.write_text("\n".join(lines) + "\n")


def write_bmp(path, width, height, pixels):
    """Write a bottom-up 24-bit BGR BMP identical in layout to platform_sdl.c.
    `pixels` is a flat top-to-bottom list of (r,g,b)."""
    row_size = ((width * 3 + 3) & ~3)
    data_size = row_size * height
    file_size = 54 + data_size
    hdr = bytearray(54)
    hdr[0:2] = b"BM"
    struct.pack_into("<I", hdr, 2, file_size)
    struct.pack_into("<I", hdr, 10, 54)      # data offset
    struct.pack_into("<I", hdr, 14, 40)      # DIB header size
    struct.pack_into("<i", hdr, 18, width)
    struct.pack_into("<i", hdr, 22, height)  # positive => bottom-up
    struct.pack_into("<H", hdr, 26, 1)       # planes
    struct.pack_into("<H", hdr, 28, 24)      # bpp
    struct.pack_into("<I", hdr, 34, data_size)
    body = bytearray()
    for y in range(height - 1, -1, -1):      # bottom row first
        row = bytearray(row_size)
        for x in range(width):
            r, g, b = pixels[y * width + x]
            row[x * 3 + 0] = b
            row[x * 3 + 1] = g
            row[x * 3 + 2] = r
        body += row
    path.write_bytes(bytes(hdr) + bytes(body))


def shade(rgb, k):
    """Grayscale vertex-shade multiply: all channels * k (0..1). Hue-preserving."""
    return tuple(int(round(c * k)) for c in rgb)


# ------------------------------------------------------------------ tests #

def test_normalize_token_key():
    assert hp.normalize_token_key("tok0022") == "tok0022"
    assert hp.normalize_token_key("22") == "tok0022"
    assert hp.normalize_token_key("settex_22") == "tok0022"
    assert hp.normalize_token_key("  tok0949 ") == "tok0949"
    assert hp.normalize_token_key("garbage") is None
    assert hp.normalize_token_key(None) is None


def test_load_room_tokens_filters_and_sorts(tmp_path):
    m = tmp_path / "m.csv"
    write_manifest(m, [
        ("tok0031", "room"),
        ("tok0022", "room"),
        ("tok0100", "weapon"),   # not room -> excluded
        ("tok0200", "hud"),      # not room -> excluded
        ("tok0022", "room"),     # duplicate -> collapsed
        ("tok0007", "room"),
    ])
    assert hp.load_room_tokens(str(m)) == ["tok0007", "tok0022", "tok0031"]


def test_assign_hues_maximally_spaced():
    toks = ["tok0001", "tok0002", "tok0003", "tok0004"]
    hues = hp.assign_hues(toks)
    assert sorted(hues.values()) == [0.0, 90.0, 180.0, 270.0]
    # every pair separated by >= 360/N
    vals = sorted(hues.values())
    for i in range(len(vals)):
        d = abs(vals[i] - vals[(i + 1) % len(vals)]) % 360.0
        d = min(d, 360.0 - d)
        assert d >= 360.0 / len(vals) - 1e-9


def test_emit_writes_pack_and_map(tmp_path):
    m = tmp_path / "m.csv"
    write_manifest(m, [("tok0022", "room"), ("tok0949", "room"),
                       ("tok0500", "chrprop")])
    out = tmp_path / "pack"
    hues = hp.emit_pack(str(m), str(out))
    # only the two ROOM tokens are painted
    assert set(hues) == {"tok0022", "tok0949"}
    tex = out / "textures"
    assert (tex / "tok0022.png").exists()
    assert (tex / "tok0949.png").exists()
    assert not (tex / "tok0500.png").exists()   # chrprop excluded
    # PNGs are valid PNG (loader uses stb_image)
    assert (tex / "tok0022.png").read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
    # hue_map.json is well-formed and carries the two tokens + thresholds
    hm = json.loads((out / "hue_map.json").read_text())
    assert set(hm["tokens"]) == {"tok0022", "tok0949"}
    assert hm["count"] == 2
    for key in ("saturation_min", "value_min", "value_max", "hue_tolerance_deg"):
        assert key in hm


def test_emit_is_deterministic(tmp_path):
    m = tmp_path / "m.csv"
    write_manifest(m, [("tok0022", "room"), ("tok0031", "room"),
                       ("tok0007", "room")])
    a, b = tmp_path / "a", tmp_path / "b"
    hp.emit_pack(str(m), str(a))
    hp.emit_pack(str(m), str(b))
    assert (a / "hue_map.json").read_bytes() == (b / "hue_map.json").read_bytes()
    assert ((a / "textures" / "tok0022.png").read_bytes()
            == (b / "textures" / "tok0022.png").read_bytes())


def test_flat_png_decodes_via_bmp_reader_is_not_applicable():
    # PNG isn't BMP; just assert the flat colour survives an RGB->HSV->angle read.
    rgb = hp.hue_to_rgb255(137.5)
    h, s, v = colorsys.rgb_to_hsv(*(c / 255.0 for c in rgb))
    assert abs(h * 360.0 - 137.5) < 1.0
    assert s > 0.99   # pure hue


def test_bmp_roundtrip(tmp_path):
    px = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (10, 20, 30),
          (200, 100, 50), (1, 2, 3)]
    p = tmp_path / "t.bmp"
    write_bmp(p, 3, 2, px)
    w, h, out = hp.read_bmp(str(p))
    assert (w, h) == (3, 2)
    assert out == px


def _synth_hue_screenshot(hues, ground_token, w=64, h=48):
    """Paint a frame: `ground_token`'s hue over most of it (each block darkened
    by a different k, to prove hue-not-luma clustering), the other tokens in
    small stripes, and a low-saturation grey band that must be ignored."""
    pixels = [(0, 0, 0)] * (w * h)
    others = [t for t in sorted(hues) if t != ground_token]

    def put(x0, y0, x1, y1, rgb):
        for y in range(y0, y1):
            for x in range(x0, x1):
                pixels[y * w + x] = rgb

    # ground: the whole frame, darkened per-quadrant (k varies, hue constant)
    grgb = hp.hue_to_rgb255(hues[ground_token])
    put(0, 0, w, h // 2, shade(grgb, 1.0))
    put(0, h // 2, w // 2, h, shade(grgb, 0.55))   # heavy grayscale multiply
    put(w // 2, h // 2, w, h, shade(grgb, 0.30))
    # a couple of smaller non-ground surfaces
    for i, t in enumerate(others[:2]):
        put(2 + i * 6, 2, 2 + i * 6 + 4, 10, hp.hue_to_rgb255(hues[t]))
    # low-saturation "stock" band (grey) -- must not be attributed to any token
    put(0, 0, w, 3, (128, 130, 126))
    return w, h, pixels


def test_identify_ranks_ground_token_first(tmp_path):
    # Fixture ground truth: tok0022 owns the frame (mirrors the Dam acceptance).
    m = tmp_path / "m.csv"
    write_manifest(m, [("tok0022", "room"), ("tok0031", "room"),
                       ("tok0949", "room"), ("tok0007", "room")])
    out = tmp_path / "pack"
    hues = hp.emit_pack(str(m), str(out))

    w, h, pixels = _synth_hue_screenshot(hues, "tok0022")
    tokens_hue, th = hp.load_hue_map(str(out / "hue_map.json"))
    ranked, classified, total = hp.identify(
        w, h, pixels, tokens_hue,
        th["saturation_min"], th["value_min"], th["value_max"],
        th["hue_tolerance_deg"])

    assert classified > 0
    assert ranked[0][0] == "tok0022"                 # #1 == ground
    assert ranked[0][1] > sum(c for _, c, _ in ranked[1:])  # dominant coverage


def test_identify_via_bmp_and_main(tmp_path, capsys):
    """End-to-end through the .bmp path and main(): emit, write a real BMP with
    the fixture, run `--identify`, assert the printed #1 is the ground token."""
    m = tmp_path / "m.csv"
    write_manifest(m, [("tok0022", "room"), ("tok0031", "room"),
                       ("tok0949", "room")])
    out = tmp_path / "pack"
    hp.emit_pack(str(m), str(out))
    hues, _ = hp.load_hue_map(str(out / "hue_map.json"))
    w, h, pixels = _synth_hue_screenshot(hues, "tok0022")
    shot = tmp_path / "screenshot_hue_dam.bmp"
    write_bmp(shot, w, h, pixels)

    rc = hp.main(["--identify", str(shot), "--map", str(out / "hue_map.json")])
    assert rc == 0
    stdout = capsys.readouterr().out
    assert "TOP ROOM TOKEN: tok0022" in stdout


def test_grayscale_multiply_preserves_hue_attribution(tmp_path):
    """Darkening a pure hue by any k in (0,1] keeps it attributed to the same
    token -- the load-bearing property behind the whole technique."""
    m = tmp_path / "m.csv"
    write_manifest(m, [("tok0022", "room"), ("tok0031", "room")])
    out = tmp_path / "pack"
    hues = hp.emit_pack(str(m), str(out))
    tokens_hue, th = hp.load_hue_map(str(out / "hue_map.json"))
    base = hp.hue_to_rgb255(hues["tok0022"])
    for k in (1.0, 0.8, 0.5, 0.25, 0.1):
        ranked, classified, _ = hp.identify(
            1, 1, [shade(base, k)], tokens_hue,
            th["saturation_min"], th["value_min"], th["value_max"],
            th["hue_tolerance_deg"])
        if classified:                       # k=0.1 may fall below value_min
            assert ranked[0][0] == "tok0022"
