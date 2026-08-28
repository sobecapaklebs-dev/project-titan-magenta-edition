#!/usr/bin/env python3
"""Generate Project Titan's compact A8 UI atlas from a locally installed font.

No source font is copied into the project. The script writes only the raster
atlas, a preview image and compact renderer metrics.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FIRST_CHARACTER = 32
LAST_CHARACTER = 126
COLUMNS = 16
CELL_WIDTH = 20
CELL_HEIGHT = 24
ATLAS_WIDTH = COLUMNS * CELL_WIDTH
GLYPH_COUNT = LAST_CHARACTER - FIRST_CHARACTER + 1
ATLAS_GLYPH_COUNT = GLYPH_COUNT + 1
STAR_CODEPOINT = 0x2605
STAR_GLYPH_INDEX = GLYPH_COUNT
ROWS = (GLYPH_COUNT + COLUMNS - 1) // COLUMNS
ATLAS_HEIGHT = ROWS * CELL_HEIGHT
BASELINE = 18
FONT_SIZE = 19
LEFT_PADDING = 1
RIGHT_PADDING = 1


def build_atlas(
    font_path: Path,
) -> tuple[Image.Image, list[int], list[int], list[int]]:
    font = ImageFont.truetype(str(font_path), size=FONT_SIZE)
    atlas = Image.new("L", (ATLAS_WIDTH, ATLAS_HEIGHT), 0)
    draw = ImageDraw.Draw(atlas)
    advances: list[int] = []
    draw_widths: list[int] = []

    glyph_lengths = {
        chr(codepoint): font.getlength(chr(codepoint))
        for codepoint in range(FIRST_CHARACTER, LAST_CHARACTER + 1)
    }

    for glyph_index, codepoint in enumerate(range(FIRST_CHARACTER, LAST_CHARACTER + 1)):
        character = chr(codepoint)
        column = glyph_index % COLUMNS
        row = glyph_index // COLUMNS
        cell_x = column * CELL_WIDTH
        cell_y = row * CELL_HEIGHT

        left, top, right, bottom = draw.textbbox(
            (0, 0), character, font=font, anchor="ls"
        )
        glyph_width = max(0, right - left)

        # Keep every visible glyph anchored to the same left edge. The old
        # centered-cell atlas combined badly with proportional advances and
        # produced visibly uneven gaps between narrow and wide letters.
        draw_x = cell_x + LEFT_PADDING - left
        draw_y = cell_y + BASELINE

        draw.text(
            (draw_x, draw_y),
            character,
            font=font,
            fill=255,
            anchor="ls",
        )

        advance = int(round(glyph_lengths[character])) + RIGHT_PADDING
        used_width = LEFT_PADDING + glyph_width + RIGHT_PADDING

        advances.append(max(4, min(CELL_WIDTH, advance)))
        draw_widths.append(max(1, min(CELL_WIDTH, used_width)))

    # U+2605 uses the single cell left free by the 16-column ASCII atlas.
    # A geometric mask makes regeneration deterministic across Inter builds
    # whose symbol fallback can otherwise differ between host systems.
    star_scale = 4
    star = Image.new(
        "L",
        (CELL_WIDTH * star_scale, CELL_HEIGHT * star_scale),
        0,
    )
    star_draw = ImageDraw.Draw(star)
    points = []
    for point in range(10):
        angle = (-math.pi / 2.0) + (point * math.pi / 5.0)
        radius = (8.0 if point % 2 == 0 else 3.45) * star_scale
        points.append(
            (
                (CELL_WIDTH * 0.5 * star_scale) + math.cos(angle) * radius,
                (CELL_HEIGHT * 0.5 * star_scale) + math.sin(angle) * radius,
            )
        )
    star_draw.polygon(points, fill=255)
    star = star.resize((CELL_WIDTH, CELL_HEIGHT), Image.Resampling.LANCZOS)
    atlas.paste(
        star,
        (
            (STAR_GLYPH_INDEX % COLUMNS) * CELL_WIDTH,
            (STAR_GLYPH_INDEX // COLUMNS) * CELL_HEIGHT,
        ),
    )

    kerning: list[int] = []

    for left_codepoint in range(FIRST_CHARACTER, LAST_CHARACTER + 1):
        left_character = chr(left_codepoint)

        for right_codepoint in range(FIRST_CHARACTER, LAST_CHARACTER + 1):
            right_character = chr(right_codepoint)
            pair_length = font.getlength(left_character + right_character)
            pair_kerning = int(
                round(
                    pair_length
                    - glyph_lengths[left_character]
                    - glyph_lengths[right_character]
                )
            )
            kerning.append(max(-8, min(7, pair_kerning)))

    return atlas, advances, draw_widths, kerning


def format_unsigned_array(values: list[int], per_line: int = 16) -> str:
    lines = []
    for index in range(0, len(values), per_line):
        lines.append(
            "    " + ", ".join(str(value) for value in values[index:index + per_line])
        )
    return ",\n".join(lines)


def format_signed_array(values: list[int], per_line: int = 32) -> str:
    lines = []
    for index in range(0, len(values), per_line):
        lines.append(
            "    " + ", ".join(str(value) for value in values[index:index + per_line])
        )
    return ",\n".join(lines)


def write_metrics(
    path: Path,
    advances: list[int],
    draw_widths: list[int],
    kerning: list[int],
) -> None:
    contents = f"""#ifndef PROJECT_TITAN_FONT_METRICS_H
#define PROJECT_TITAN_FONT_METRICS_H

#define TITAN_FONT_FIRST_CHARACTER {FIRST_CHARACTER}
#define TITAN_FONT_LAST_CHARACTER {LAST_CHARACTER}
#define TITAN_FONT_GLYPH_COUNT {GLYPH_COUNT}
#define TITAN_FONT_ATLAS_GLYPH_COUNT {ATLAS_GLYPH_COUNT}
#define TITAN_FONT_STAR_CODEPOINT 0x{STAR_CODEPOINT:04X}u
#define TITAN_FONT_STAR_GLYPH_INDEX {STAR_GLYPH_INDEX}u
#define TITAN_FONT_STAR_ADVANCE 17u
#define TITAN_FONT_STAR_DRAW_WIDTH 18u

#define TITAN_FONT_ATLAS_WIDTH {ATLAS_WIDTH}
#define TITAN_FONT_ATLAS_HEIGHT {ATLAS_HEIGHT}
#define TITAN_FONT_CELL_WIDTH {CELL_WIDTH}
#define TITAN_FONT_CELL_HEIGHT {CELL_HEIGHT}
#define TITAN_FONT_ATLAS_COLUMNS {COLUMNS}
#define TITAN_FONT_EXPECTED_BYTES {ATLAS_WIDTH * ATLAS_HEIGHT}

static const unsigned char g_titan_font_advance[TITAN_FONT_GLYPH_COUNT] =
{{
{format_unsigned_array(advances)}
}};

static const unsigned char g_titan_font_draw_width[TITAN_FONT_GLYPH_COUNT] =
{{
{format_unsigned_array(draw_widths)}
}};

static const signed char g_titan_font_kerning[
    TITAN_FONT_GLYPH_COUNT * TITAN_FONT_GLYPH_COUNT
] =
{{
{format_signed_array(kerning)}
}};

#endif
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument(
        "--atlas",
        type=Path,
        default=Path("assets/ui/titan_ui_font.a8"),
    )
    parser.add_argument(
        "--preview",
        type=Path,
        default=Path("assets/ui/titan_ui_font_preview.png"),
    )
    parser.add_argument(
        "--metrics",
        type=Path,
        default=Path("src/graphics/titan_font_metrics.h"),
    )
    args = parser.parse_args()

    if not args.font.is_file():
        raise SystemExit(f"font not found: {args.font}")

    atlas, advances, draw_widths, kerning = build_atlas(args.font)

    args.atlas.parent.mkdir(parents=True, exist_ok=True)
    args.preview.parent.mkdir(parents=True, exist_ok=True)
    args.atlas.write_bytes(atlas.tobytes())
    atlas.save(args.preview)
    write_metrics(args.metrics, advances, draw_widths, kerning)

    print(f"atlas:   {args.atlas}")
    print(f"preview: {args.preview}")
    print(f"metrics: {args.metrics}")


if __name__ == "__main__":
    main()
