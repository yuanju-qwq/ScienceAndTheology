from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageFilter


@dataclass(frozen=True)
class Box:
    min_x: int
    min_y: int
    max_x: int
    max_y: int

    @property
    def width(self) -> int:
        return self.max_x - self.min_x + 1

    @property
    def height(self) -> int:
        return self.max_y - self.min_y + 1


RESAMPLE_FILTERS = {
    "nearest": Image.Resampling.NEAREST,
    "box": Image.Resampling.BOX,
    "bilinear": Image.Resampling.BILINEAR,
    "bicubic": Image.Resampling.BICUBIC,
    "lanczos": Image.Resampling.LANCZOS,
}


def parse_color(value: str) -> tuple[int, int, int]:
    parts = [int(part.strip()) for part in value.split(",")]
    if len(parts) != 3 or any(part < 0 or part > 255 for part in parts):
        raise argparse.ArgumentTypeError("color must be R,G,B with values from 0 to 255")
    return parts[0], parts[1], parts[2]


def is_key_pixel(
    pixel: tuple[int, int, int, int],
    key: tuple[int, int, int],
    tolerance: float,
    remove_key_spill: bool = False,
) -> bool:
    r, g, b, a = pixel
    if a == 0:
        return True

    distance = math.sqrt((r - key[0]) ** 2 + (g - key[1]) ** 2 + (b - key[2]) ** 2)
    if distance <= tolerance:
        return True

    if remove_key_spill and key == (255, 0, 255):
        red_blue_max = max(r, b)
        red_blue_min = min(r, b)
        red_blue_close = abs(r - b) <= max(10, red_blue_max * 0.25)
        green_suppressed = g <= red_blue_min * 0.55
        if red_blue_min >= 10 and red_blue_close and green_suppressed:
            return True

    return False


def content_bounds(
    image: Image.Image,
    region: Box,
    key: tuple[int, int, int],
    tolerance: float,
    remove_key_spill: bool,
) -> Box | None:
    pixels = image.load()
    min_x = region.width
    min_y = region.height
    max_x = -1
    max_y = -1

    for y in range(region.min_y, region.max_y + 1):
        for x in range(region.min_x, region.max_x + 1):
            if not is_key_pixel(pixels[x, y], key, tolerance, remove_key_spill):
                local_x = x - region.min_x
                local_y = y - region.min_y
                min_x = min(min_x, local_x)
                min_y = min(min_y, local_y)
                max_x = max(max_x, local_x)
                max_y = max(max_y, local_y)

    if max_x < min_x or max_y < min_y:
        return None

    return Box(
        region.min_x + min_x,
        region.min_y + min_y,
        region.min_x + max_x,
        region.min_y + max_y,
    )


def grid_boxes(
    image: Image.Image,
    columns: int,
    rows: int,
    key: tuple[int, int, int],
    tolerance: float,
    remove_key_spill: bool,
) -> list[Box]:
    cell_width = image.width // columns
    cell_height = image.height // rows
    boxes: list[Box] = []

    for row in range(rows):
        for column in range(columns):
            min_x = column * cell_width
            min_y = row * cell_height
            max_x = image.width - 1 if column == columns - 1 else ((column + 1) * cell_width) - 1
            max_y = image.height - 1 if row == rows - 1 else ((row + 1) * cell_height) - 1
            bounds = content_bounds(image, Box(min_x, min_y, max_x, max_y), key, tolerance, remove_key_spill)
            if bounds is None:
                raise ValueError(f"no content found in grid cell column={column} row={row}")
            boxes.append(bounds)

    return boxes


def object_boxes_by_columns(
    image: Image.Image,
    key: tuple[int, int, int],
    tolerance: float,
    remove_key_spill: bool,
) -> list[Box]:
    pixels = image.load()
    column_has_content = [False] * image.width

    for y in range(image.height):
        for x in range(image.width):
            if not is_key_pixel(pixels[x, y], key, tolerance, remove_key_spill):
                column_has_content[x] = True

    runs: list[tuple[int, int]] = []
    start = -1
    for x, has_content in enumerate(column_has_content):
        if has_content and start < 0:
            start = x
        if (not has_content or x == image.width - 1) and start >= 0:
            runs.append((start, x if has_content and x == image.width - 1 else x - 1))
            start = -1

    boxes: list[Box] = []
    for start_x, end_x in runs:
        bounds = content_bounds(image, Box(start_x, 0, end_x, image.height - 1), key, tolerance, remove_key_spill)
        if bounds is not None:
            boxes.append(bounds)

    return boxes


def cutout(
    image: Image.Image,
    box: Box,
    key: tuple[int, int, int],
    tolerance: float,
    remove_key_spill: bool,
) -> Image.Image:
    pixels = image.load()
    output = Image.new("RGBA", (box.width, box.height), (0, 0, 0, 0))
    output_pixels = output.load()

    for y in range(box.height):
        for x in range(box.width):
            pixel = pixels[box.min_x + x, box.min_y + y]
            if not is_key_pixel(pixel, key, tolerance, remove_key_spill):
                output_pixels[x, y] = pixel

    return output


def fit_sprite(
    sprite: Image.Image,
    target_width: int,
    target_height: int,
    padding: int,
    resample: int,
    sharpen_radius: float,
    sharpen_percent: int,
    sharpen_threshold: int,
    native_only: bool,
) -> Image.Image:
    if native_only and (sprite.width != target_width or sprite.height != target_height):
        raise ValueError(
            "sprite source is "
            f"{sprite.width}x{sprite.height}, expected native {target_width}x{target_height}; "
            "regenerate the source at the exact target size or remove --native-only"
        )

    available_width = max(1, target_width - (padding * 2))
    available_height = max(1, target_height - (padding * 2))
    scale = min(available_width / sprite.width, available_height / sprite.height)
    draw_width = max(1, int(math.floor(sprite.width * scale)))
    draw_height = max(1, int(math.floor(sprite.height * scale)))
    left = (target_width - draw_width) // 2
    top = (target_height - draw_height) // 2

    resized = sprite.resize((draw_width, draw_height), resample=resample)
    if sharpen_percent > 0:
        resized = resized.filter(
            ImageFilter.UnsharpMask(
                radius=sharpen_radius,
                percent=sharpen_percent,
                threshold=sharpen_threshold,
            )
        )
    output = Image.new("RGBA", (target_width, target_height), (0, 0, 0, 0))
    output.alpha_composite(resized, (left, top))
    return output


def write_contact_sheet(images: list[Image.Image], output_path: Path, scale: int = 4) -> None:
    gap = 8
    tile_width = images[0].width * scale
    tile_height = images[0].height * scale
    sheet = Image.new(
        "RGBA",
        (len(images) * tile_width + (len(images) + 1) * gap, tile_height + (gap * 2)),
        (30, 30, 34, 255),
    )

    for index, image in enumerate(images):
        preview = image.resize((tile_width, tile_height), resample=Image.Resampling.NEAREST)
        sheet.alpha_composite(preview, (gap + index * (tile_width + gap), gap))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Slice magenta-keyed sprites from a source sheet.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--names-csv", required=True)
    parser.add_argument("--mode", choices=("grid", "objects-x"), default="grid")
    parser.add_argument("--columns", type=int, default=1)
    parser.add_argument("--rows", type=int, default=1)
    parser.add_argument("--target-width", type=int, default=32)
    parser.add_argument("--target-height", type=int, default=32)
    parser.add_argument("--padding", type=int, default=2)
    parser.add_argument("--key-color", type=parse_color, default=(255, 0, 255))
    parser.add_argument("--key-tolerance", type=float, default=90.0)
    parser.add_argument("--remove-key-spill", action="store_true")
    parser.add_argument("--resample", choices=tuple(RESAMPLE_FILTERS.keys()), default="lanczos")
    parser.add_argument("--sharpen-radius", type=float, default=0.0)
    parser.add_argument("--sharpen-percent", type=int, default=0)
    parser.add_argument("--sharpen-threshold", type=int, default=0)
    parser.add_argument("--native-only", action="store_true", help="Reject sprites that are not already target size.")
    parser.add_argument("--contact-sheet", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    names = [name.strip() for name in args.names_csv.split(",") if name.strip()]
    if not names:
        raise ValueError("at least one output name is required")

    source = Image.open(args.input).convert("RGBA")
    if args.mode == "grid":
        if args.columns <= 0 or args.rows <= 0:
            raise ValueError("columns and rows must be positive")
        boxes = grid_boxes(source, args.columns, args.rows, args.key_color, args.key_tolerance, args.remove_key_spill)
    else:
        boxes = object_boxes_by_columns(source, args.key_color, args.key_tolerance, args.remove_key_spill)

    if len(boxes) != len(names):
        raise ValueError(f"expected {len(names)} sprites, detected {len(boxes)}")

    args.output_directory.mkdir(parents=True, exist_ok=True)
    outputs: list[Image.Image] = []
    resample_filter = RESAMPLE_FILTERS[args.resample]

    print(f"[assets] Slicing {args.input} -> {args.output_directory}")
    for name, box in zip(names, boxes):
        sprite = cutout(source, box, args.key_color, args.key_tolerance, args.remove_key_spill)
        output = fit_sprite(
            sprite,
            args.target_width,
            args.target_height,
            args.padding,
            resample_filter,
            args.sharpen_radius,
            args.sharpen_percent,
            args.sharpen_threshold,
            args.native_only,
        )
        outputs.append(output)

        output_path = args.output_directory / name
        print(
            "[assets] "
            f"{name}: source_box=({box.min_x},{box.min_y})-({box.max_x},{box.max_y}) "
            f"source_size={box.width}x{box.height}"
        )
        if not args.dry_run:
            output.save(output_path)

    if args.contact_sheet is not None:
        write_contact_sheet(outputs, args.contact_sheet)
        print(f"[assets] Contact sheet: {args.contact_sheet}")


if __name__ == "__main__":
    main()
