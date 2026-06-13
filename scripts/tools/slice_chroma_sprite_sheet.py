from __future__ import annotations

import argparse
import math
from pathlib import Path

from png_pixel_tools import (
    bounding_box_non_key,
    chroma_cutout,
    crop,
    new_image,
    paste,
    read_png,
    resize_nearest,
    write_png,
)


def slice_sheet(
    input_path: Path,
    output_directory: Path,
    columns: int,
    rows: int,
    names_csv: str,
    target_width: int,
    target_height: int,
    padding: int,
    key_tolerance: int,
) -> None:
    if columns <= 0 or rows <= 0:
        raise ValueError("Columns and rows must be positive.")
    if target_width <= 0 or target_height <= 0:
        raise ValueError("Target dimensions must be positive.")

    names = [name.strip() for name in names_csv.split(",") if name.strip()]
    expected = columns * rows
    if len(names) != expected:
        raise ValueError(f"Expected {expected} output names, received {len(names)}.")

    input_path = input_path.resolve()
    output_directory = output_directory.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)

    print(f"[assets] Slicing '{input_path}' into {len(names)} sprites.")

    source_width, source_height, source = read_png(input_path)
    cell_width = source_width // columns
    cell_height = source_height // rows
    if cell_width <= 0 or cell_height <= 0:
        raise ValueError("Source image is smaller than the requested grid.")

    for index, name in enumerate(names):
        column = index % columns
        row = index // columns
        cell = crop(
            source,
            source_width,
            source_height,
            column * cell_width,
            row * cell_height,
            cell_width,
            cell_height,
        )

        bbox = bounding_box_non_key(cell, cell_width, cell_height, tolerance=key_tolerance)
        if bbox is None:
            raise ValueError(f"No non-key pixels found for sprite '{name}'.")
        min_x, min_y, max_x, max_y = bbox
        content_width = max_x - min_x + 1
        content_height = max_y - min_y + 1
        content = crop(cell, cell_width, cell_height, min_x, min_y, content_width, content_height)
        content = chroma_cutout(content, tolerance=key_tolerance)

        available_width = max(1, target_width - (2 * padding))
        available_height = max(1, target_height - (2 * padding))
        scale = min(available_width / content_width, available_height / content_height)
        draw_width = max(1, math.floor(content_width * scale))
        draw_height = max(1, math.floor(content_height * scale))
        draw_x = (target_width - draw_width) // 2
        draw_y = (target_height - draw_height) // 2

        resized = resize_nearest(content, content_width, content_height, draw_width, draw_height)
        output = new_image(target_width, target_height)
        paste(output, target_width, target_height, resized, draw_width, draw_height, draw_x, draw_y)
        write_png(output_directory / name, target_width, target_height, output)

    print(f"[assets] Wrote {len(names)} sprites to '{output_directory}'.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Slice a magenta-keyed sprite sheet into PNG sprites.")
    parser.add_argument("input_path", type=Path)
    parser.add_argument("output_directory", type=Path)
    parser.add_argument("columns", type=int)
    parser.add_argument("rows", type=int)
    parser.add_argument("names_csv")
    parser.add_argument("--target-width", type=int, default=32)
    parser.add_argument("--target-height", type=int, default=32)
    parser.add_argument("--padding", type=int, default=2)
    parser.add_argument("--key-tolerance", type=int, default=90)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    slice_sheet(
        args.input_path,
        args.output_directory,
        args.columns,
        args.rows,
        args.names_csv,
        args.target_width,
        args.target_height,
        args.padding,
        args.key_tolerance,
    )


if __name__ == "__main__":
    main()
