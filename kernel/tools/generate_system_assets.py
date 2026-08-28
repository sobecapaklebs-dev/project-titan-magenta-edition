#!/usr/bin/env python3
"""Generate deterministic fallback Project Titan UI masks and avatars.

These procedural assets are only a fallback/test set. The curated runtime asset
tree is protected and is never overwritten unless an explicit force flag is
provided.
"""

from __future__ import annotations

import argparse
import csv
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw

SCALE = 4
MAGENTA = 255


def canvas(width: int, height: int) -> Image.Image:
    return Image.new("L", (width * SCALE, height * SCALE), 0)


def scaled(points):
    return tuple(tuple(int(round(value * SCALE)) for value in point) for point in points)


def line(draw: ImageDraw.ImageDraw, points, width: int = 5, fill: int = MAGENTA):
    draw.line(scaled(points), fill=fill, width=width * SCALE, joint="curve")


def ellipse(draw: ImageDraw.ImageDraw, box, width: int = 5, fill: int = MAGENTA):
    draw.ellipse(tuple(int(v * SCALE) for v in box), outline=fill, width=width * SCALE)


def rectangle(draw: ImageDraw.ImageDraw, box, width: int = 5, radius: int = 0, fill: int = MAGENTA):
    target = tuple(int(v * SCALE) for v in box)
    if radius:
        draw.rounded_rectangle(target, radius=radius * SCALE, outline=fill, width=width * SCALE)
    else:
        draw.rectangle(target, outline=fill, width=width * SCALE)


def polygon(draw: ImageDraw.ImageDraw, points, fill: int = MAGENTA):
    draw.polygon(scaled(points), fill=fill)


def downsample(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    return image.resize(size, Image.Resampling.LANCZOS)


def icon_overview(draw):
    ellipse(draw, (28, 28, 100, 100), 5)
    line(draw, ((64, 64), (64, 31)), 5)
    line(draw, ((64, 64), (95, 82)), 5)
    line(draw, ((64, 64), (36, 87)), 5)
    ellipse(draw, (57, 57, 71, 71), 4)


def icon_applications(draw):
    for row in range(2):
        for col in range(2):
            rectangle(draw, (31 + col * 37, 31 + row * 37, 59 + col * 37, 59 + row * 37), 5, 5)


def icon_controller(draw):
    line(draw, ((31, 78), (38, 51), (51, 43), (77, 43), (90, 51), (97, 78)), 6)
    line(draw, ((31, 78), (43, 86), (56, 72), (72, 72), (85, 86), (97, 78)), 6)
    line(draw, ((47, 57), (47, 72)), 5)
    line(draw, ((39, 64), (55, 64)), 5)
    ellipse(draw, (76, 55, 84, 63), 3)
    ellipse(draw, (86, 65, 94, 73), 3)


def icon_user(draw):
    ellipse(draw, (48, 25, 80, 57), 5)
    line(draw, ((32, 96), (38, 74), (51, 64), (77, 64), (90, 74), (96, 96)), 6)
    line(draw, ((32, 96), (96, 96)), 6)


def icon_saved_data(draw):
    rectangle(draw, (31, 30, 83, 98), 5, 6)
    line(draw, ((44, 47), (70, 47)), 5)
    line(draw, ((44, 61), (65, 61)), 5)
    line(draw, ((79, 78), (100, 78)), 6)
    line(draw, ((91, 67), (102, 78), (91, 89)), 6)


def gear(draw, cx=64, cy=64, radius=27):
    ellipse(draw, (cx - radius, cy - radius, cx + radius, cy + radius), 5)
    ellipse(draw, (cx - 8, cy - 8, cx + 8, cy + 8), 5)
    for angle in range(0, 360, 45):
        a = math.radians(angle)
        x1 = cx + math.cos(a) * (radius + 1)
        y1 = cy + math.sin(a) * (radius + 1)
        x2 = cx + math.cos(a) * (radius + 14)
        y2 = cy + math.sin(a) * (radius + 14)
        line(draw, ((x1, y1), (x2, y2)), 7)


def icon_profile_settings(draw):
    ellipse(draw, (28, 29, 54, 55), 4)
    line(draw, ((20, 87), (25, 69), (35, 61), (49, 61), (58, 68)), 5)
    line(draw, ((20, 87), (53, 87)), 5)
    gear(draw, 83, 72, 18)


def icon_settings(draw):
    gear(draw)


def icon_rune(draw):
    polygon(draw, ((64, 19), (101, 51), (88, 101), (64, 111), (40, 101), (27, 51)))
    polygon(draw, ((64, 34), (84, 55), (75, 89), (64, 96), (53, 89), (44, 55)), fill=0)
    line(draw, ((64, 35), (64, 95)), 4)
    line(draw, ((45, 56), (83, 56)), 4)


def icon_notifications(draw):
    line(draw, ((36, 86), (44, 77), (44, 54), (50, 39), (64, 33), (78, 39), (84, 54), (84, 77), (92, 86)), 6)
    line(draw, ((36, 86), (92, 86)), 6)
    ellipse(draw, (57, 91, 71, 105), 4)


def icon_power(draw):
    ellipse(draw, (30, 29, 98, 99), 6)
    # erase top gap, then draw stem
    polygon(draw, ((52, 20), (76, 20), (76, 50), (52, 50)), fill=0)
    line(draw, ((64, 18), (64, 62)), 7)


def icon_vmc(draw):
    rectangle(draw, (36, 22, 92, 106), 5, 5)
    rectangle(draw, (47, 34, 81, 56), 4, 2)
    for x in (47, 58, 69, 80):
        line(draw, ((x, 82), (x, 96)), 4)


def icon_debug_settings(draw):
    # Two rotating arrows, matching the supplied Debug Settings artwork while
    # remaining deterministic when the procedural fallback set is rebuilt.
    draw.arc(
        tuple(int(v * SCALE) for v in (27, 29, 101, 103)),
        start=202,
        end=25,
        fill=MAGENTA,
        width=8 * SCALE,
    )
    polygon(draw, ((85, 24), (107, 27), (99, 48)))
    draw.arc(
        tuple(int(v * SCALE) for v in (27, 25, 101, 99)),
        start=22,
        end=205,
        fill=MAGENTA,
        width=8 * SCALE,
    )
    polygon(draw, ((43, 104), (21, 101), (29, 80)))


ICON_DRAWERS = (
    icon_overview,
    icon_applications,
    icon_controller,
    icon_user,
    icon_saved_data,
    icon_profile_settings,
    icon_settings,
    icon_rune,
    icon_notifications,
    icon_power,
    icon_vmc,
    icon_debug_settings,
)


def save_a8(image: Image.Image, raw: Path, preview: Path) -> None:
    raw.parent.mkdir(parents=True, exist_ok=True)
    preview.parent.mkdir(parents=True, exist_ok=True)
    raw.write_bytes(image.tobytes())
    image.save(preview, optimize=True)


def make_single(drawer, size=(128, 128)) -> Image.Image:
    image = canvas(*size)
    drawer(ImageDraw.Draw(image))
    return downsample(image, size)


def make_system_icons(base: Path) -> None:
    atlas = Image.new("L", (640, 384), 0)
    for index, drawer in enumerate(ICON_DRAWERS):
        icon = make_single(drawer)
        atlas.paste(icon, ((index % 5) * 128, (index // 5) * 128))
    save_a8(atlas, base / "titan_ui_icons.a8", base / "titan_ui_icons_preview.png")

    alert = canvas(128, 128)
    d = ImageDraw.Draw(alert)
    polygon(d, ((64, 17), (111, 105), (17, 105)))
    polygon(d, ((64, 33), (96, 96), (32, 96)), fill=0)
    line(d, ((64, 48), (64, 74)), 7)
    ellipse(d, (60, 82, 68, 90), 3)
    save_a8(downsample(alert, (128, 128)), base / "titan_ui_alert.a8", base / "titan_ui_alert.png")

    app = canvas(128, 128)
    icon_applications(ImageDraw.Draw(app))
    save_a8(downsample(app, (128, 128)), base / "titan_ui_app_home.a8", base / "titan_ui_app_home.png")

    transfer = canvas(128, 128)
    d = ImageDraw.Draw(transfer)
    line(d, ((24, 45), (94, 45)), 7)
    line(d, ((80, 31), (96, 45), (80, 59)), 7)
    line(d, ((104, 83), (34, 83)), 7)
    line(d, ((48, 69), (32, 83), (48, 97)), 7)
    save_a8(downsample(transfer, (128, 128)), base / "titan_ui_ftp_home.a8", base / "titan_ui_ftp_home.png")

    debug_settings = make_single(icon_debug_settings)
    save_a8(
        debug_settings,
        base / "titan_ui_debug_settings.a8",
        base / "titan_ui_debug_settings.png",
    )

    game = make_single(icon_controller)
    save_a8(game, base / "titan_ui_game_placeholder.a8", base / "titan_ui_game_placeholder.png")

    boot_raw = base / "titan_ui_boot_ps2logo.a8"
    boot_preview = base / "titan_ui_boot_ps2logo_preview.png"
    if boot_raw.exists() and boot_raw.stat().st_size == 256 * 96:
        # The platform boot mark is a separately supplied project asset. Keep
        # it intact when refreshing the deterministic first-party UI set.
        Image.frombytes("L", (256, 96), boot_raw.read_bytes()).save(
            boot_preview, optimize=True
        )
    else:
        boot = canvas(256, 96)
        d = ImageDraw.Draw(boot)
        polygon(d, ((30, 18), (73, 18), (52, 48), (73, 78), (30, 78), (48, 48)))
        line(d, ((91, 22), (227, 22)), 7)
        line(d, ((91, 48), (205, 48)), 7)
        line(d, ((91, 74), (227, 74)), 7)
        save_a8(downsample(boot, (256, 96)), boot_raw, boot_preview)

    power = canvas(256, 192)
    d = ImageDraw.Draw(power)
    ellipse(d, (70, 38, 186, 154), 9)
    polygon(d, ((110, 22), (146, 22), (146, 88), (110, 88)), fill=0)
    line(d, ((128, 20), (128, 94)), 11)
    save_a8(downsample(power, (256, 192)), base / "titan_ui_power_ps2slim.a8", base / "titan_ui_power_ps2slim_preview.png")


def pack_psm16(image: Image.Image) -> bytes:
    rgb = image.convert("RGB")
    pixels = rgb.load()
    packed = bytearray()
    for y in range(rgb.height):
        for x in range(rgb.width):
            red, green, blue = pixels[x, y]
            value = 0x8000 | (red >> 3) | ((green >> 3) << 5) | ((blue >> 3) << 10)
            packed.extend(struct.pack("<H", value))
    return bytes(packed)


def avatar_cell(avatar_id: int) -> Image.Image:
    size = 128
    hue = (avatar_id * 47) % 360
    # Fixed project palette derived without external artwork.
    palette = (
        (222, 32, 132), (174, 38, 190), (112, 54, 202),
        (44, 112, 196), (33, 164, 171), (76, 177, 112),
        (211, 139, 49), (205, 70, 75),
    )
    accent = palette[(avatar_id + hue // 45) % len(palette)]
    dark = (18, 20, 25)
    image = Image.new("RGB", (size, size), dark)
    d = ImageDraw.Draw(image)

    inset = 8 + (avatar_id % 7)
    d.rounded_rectangle((inset, inset, size - inset, size - inset), radius=22, fill=accent)
    d.ellipse((34, 25, 94, 85), fill=(238, 238, 244))
    d.rounded_rectangle((25, 75, 103, 118), radius=22, fill=(238, 238, 244))

    # Abstract visor and project rune; parameters vary deterministically.
    visor_y = 48 + (avatar_id % 8)
    d.rounded_rectangle((43, visor_y, 85, visor_y + 12), radius=6, fill=dark)
    if avatar_id % 3 == 0:
        d.polygon(((64, 17), (77, 31), (64, 43), (51, 31)), fill=dark)
    elif avatar_id % 3 == 1:
        d.ellipse((54, 20, 74, 40), outline=dark, width=6)
    else:
        d.rectangle((56, 20, 72, 40), outline=dark, width=5)

    return image.resize((32, 32), Image.Resampling.LANCZOS)


def make_avatars(base: Path) -> None:
    atlas = Image.new("RGB", (512, 512), (18, 20, 25))
    rows = []
    for avatar_id in range(199):
        cell = avatar_cell(avatar_id)
        atlas.paste(cell, ((avatar_id % 16) * 32, (avatar_id // 16) * 32))
        rows.append((avatar_id, "procedural-titan", avatar_id % 8, avatar_id % 3))

    (base / "titan_ui_avatars.psm16").write_bytes(pack_psm16(atlas))
    atlas.save(base / "titan_ui_avatars_preview.png", optimize=True)
    with (base / "titan_ui_avatars_manifest.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(("avatar_id", "origin", "palette_variant", "rune_variant"))
        writer.writerows(rows)


def main() -> int:
    source_asset_dir = Path(__file__).resolve().parents[1] / "assets" / "ui"
    default_output = Path(__file__).resolve().parents[2] / "build" / "fallback-system-assets"

    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=default_output)
    parser.add_argument(
        "--force-curated-overwrite",
        action="store_true",
        help="explicitly allow replacing kernel/assets/ui with fallback assets",
    )
    args = parser.parse_args()

    output = args.output.resolve()
    if output == source_asset_dir.resolve() and not args.force_curated_overwrite:
        parser.error(
            "refuses to overwrite curated kernel/assets/ui; choose another "
            "--output or pass --force-curated-overwrite explicitly"
        )

    output.mkdir(parents=True, exist_ok=True)
    make_system_icons(output)
    make_avatars(output)
    print(f"generated fallback Titan UI assets in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
