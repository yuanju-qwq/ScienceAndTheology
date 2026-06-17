#!/usr/bin/env python3
"""Build item icon assets for tree species.

Generates solid-color placeholder 64x64 PNG icons for tree species items
(log, plank, sapling, fruit). Each icon is a solid color square on a
transparent background, centered with padding.

Output directory: resource/items/trees/

Usage:
    python scripts/tools/build_tree_item_icons.py
"""

import os
import struct
import zlib
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
OUTPUT_DIR = PROJECT_ROOT / "resource" / "items" / "trees"

CANVAS_SIZE = 64
PADDING = 8

# Tree species: (key, wood_rgb, leaves_rgb, fruit_rgb_or_None)
TREE_SPECIES = [
    ("oak",     (115, 69, 31),  (54, 107, 38),  None),
    ("birch",   (204, 199, 179),(64, 140, 51),   None),
    ("spruce",  (89, 56, 26),   (26, 77, 31),    None),
    ("acacia",  (140, 89, 38),  (77, 140, 38),   None),
    ("maple",   (107, 64, 26),  (51, 128, 46),   None),
    ("sequoia", (102, 59, 26),  (31, 82, 26),    None),
    ("cherry",  (128, 71, 56),  (179, 89, 128),  (217, 38, 64)),
    ("olive",   (133, 102, 56), (64, 96, 38),    (140, 148, 51)),
]


def create_png(width, height, pixels):
    """Create a PNG from RGBA pixel data (row-major)."""
    def make_chunk(chunk_type, data):
        chunk = chunk_type + data
        crc = struct.pack(">I", zlib.crc32(chunk) & 0xFFFFFFFF)
        return struct.pack(">I", len(data)) + chunk + crc

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr_data = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    ihdr = make_chunk(b"IHDR", ihdr_data)

    raw_rows = b""
    for y in range(height):
        raw_rows += b"\x00"
        for x in range(width):
            r, g, b, a = pixels[y * width + x]
            raw_rows += struct.pack("BBBB", r, g, b, a)
    compressed = zlib.compress(raw_rows)
    idat = make_chunk(b"IDAT", compressed)
    iend = make_chunk(b"IEND", b"")

    return sig + ihdr + idat + iend


def make_solid_icon(color_rgb):
    """Generate a solid-color square icon with transparent padding."""
    pixels = [(0, 0, 0, 0)] * (CANVAS_SIZE * CANVAS_SIZE)
    r, g, b = color_rgb
    for y in range(PADDING, CANVAS_SIZE - PADDING):
        for x in range(PADDING, CANVAS_SIZE - PADDING):
            pixels[y * CANVAS_SIZE + x] = (r, g, b, 255)
    return pixels


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    icon_types = [
        ("log",     lambda s, w, l, f: w),
        ("plank",   lambda s, w, l, f: tuple(min(255, c + 20) for c in w)),
        ("sapling", lambda s, w, l, f: l),
        ("fruit",   lambda s, w, l, f: f),
    ]

    for species_key, wood_rgb, leaves_rgb, fruit_rgb in TREE_SPECIES:
        for icon_type, color_fn in icon_types:
            color = color_fn(species_key, wood_rgb, leaves_rgb, fruit_rgb)
            if color is None:
                continue
            pixels = make_solid_icon(color)
            path = OUTPUT_DIR / f"{icon_type}_{species_key}_icon_64.png"
            path.write_bytes(create_png(CANVAS_SIZE, CANVAS_SIZE, pixels))
            print(f"  {path.name}")

    print(f"\nDone! Icons in {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
