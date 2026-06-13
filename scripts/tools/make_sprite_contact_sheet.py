from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a simple labeled sprite contact sheet.")
    parser.add_argument("--names-csv", required=True)
    parser.add_argument("--rows", required=True, help="Semicolon-separated LABEL=DIR entries.")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--scale", type=int, default=5)
    args = parser.parse_args()

    names = [name.strip() for name in args.names_csv.split(",") if name.strip()]
    rows: list[tuple[str, Path]] = []
    for entry in args.rows.split(";"):
        label, directory = entry.split("=", 1)
        rows.append((label.strip(), Path(directory.strip())))

    if not names or not rows:
        raise ValueError("names and rows are required")

    gap = 10
    label_width = 76
    tile_size = 32 * args.scale
    width = label_width + len(names) * (tile_size + gap) + gap
    height = len(rows) * (tile_size + gap) + gap
    sheet = Image.new("RGBA", (width, height), (30, 30, 34, 255))
    draw = ImageDraw.Draw(sheet)

    for row_index, (label, directory) in enumerate(rows):
        y = gap + row_index * (tile_size + gap)
        draw.text((8, y + tile_size // 2 - 6), label, fill=(230, 230, 230, 255))
        for column_index, name in enumerate(names):
            image = Image.open(directory / name).convert("RGBA")
            preview = image.resize((tile_size, tile_size), Image.Resampling.NEAREST)
            x = label_width + gap + column_index * (tile_size + gap)
            sheet.alpha_composite(preview, (x, y))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output)
    print(args.output)


if __name__ == "__main__":
    main()
