from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image


DEFAULT_ICON_SIZE = (64, 64)
DEFAULT_FILL_RATIO = 0.88


def content_bounds(image: Image.Image) -> tuple[int, int, int, int] | None:
    alpha = image.getchannel("A")
    return alpha.getbbox()


def export_icon(
    source: Path,
    destination: Path,
    icon_size: tuple[int, int],
    fill_ratio: float,
    resample: Image.Resampling,
) -> None:
    image = Image.open(source).convert("RGBA")
    bbox = content_bounds(image)
    if bbox is None:
        raise ValueError(f"{source} has no visible pixels")

    cropped = image.crop(bbox)
    target_width, target_height = icon_size
    max_width = max(1, math.floor(target_width * fill_ratio))
    max_height = max(1, math.floor(target_height * fill_ratio))
    scale = min(max_width / cropped.width, max_height / cropped.height, 1.0 if cropped.size == icon_size else float("inf"))
    draw_width = max(1, math.floor(cropped.width * scale))
    draw_height = max(1, math.floor(cropped.height * scale))
    resized = cropped.resize((draw_width, draw_height), resample=resample)

    output = Image.new("RGBA", icon_size, (0, 0, 0, 0))
    left = (target_width - draw_width) // 2
    top = (target_height - draw_height) // 2
    output.alpha_composite(resized, (left, top))

    destination.parent.mkdir(parents=True, exist_ok=True)
    output.save(destination)


def build_item_icons(
    project_root: Path,
    source_name: str,
    icon_size: tuple[int, int],
    fill_ratio: float,
    resample: Image.Resampling,
) -> None:
    project_root = project_root.resolve()
    source_root = project_root / "resource" / "source" / source_name
    output_root = project_root / "resource" / "items"

    if not source_root.exists():
        raise FileNotFoundError(
            f"missing {source_root}; generate item icon sources there first"
        )

    copied = 0
    for source in sorted(source_root.rglob("*.png")):
        relative = source.relative_to(source_root)
        export_icon(source, output_root / relative, icon_size, fill_ratio, resample)
        copied += 1

    if copied == 0:
        raise ValueError(f"no PNG icons found in {source_root}")

    print(
        f"[assets] Exported {copied} item icons from '{source_root}' to '{output_root}' "
        f"as {icon_size[0]}x{icon_size[1]} fill={fill_ratio:.2f}."
    )


def verify_item_icons(project_root: Path, icon_size: tuple[int, int]) -> None:
    project_root = project_root.resolve()
    output_root = project_root / "resource" / "items"
    failures: list[str] = []

    for icon in sorted(output_root.rglob("*.png")):
        image = Image.open(icon)
        if image.size != icon_size:
            failures.append(f"{icon.relative_to(project_root)}: {image.size[0]}x{image.size[1]}")

    if failures:
        raise ValueError(f"item icons with unexpected canvas size found:\n" + "\n".join(failures))

    print(f"[assets] Verified all item icons under '{output_root}' are {icon_size[0]}x{icon_size[1]}.")


def parse_icon_size(value: str) -> tuple[int, int]:
    width_text, height_text = value.lower().split("x", 1)
    width = int(width_text)
    height = int(height_text)
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("icon size must be positive")
    return width, height


def parse_resample(value: str) -> Image.Resampling:
    by_name = {
        "nearest": Image.Resampling.NEAREST,
        "box": Image.Resampling.BOX,
        "bilinear": Image.Resampling.BILINEAR,
        "bicubic": Image.Resampling.BICUBIC,
        "lanczos": Image.Resampling.LANCZOS,
    }
    try:
        return by_name[value]
    except KeyError as exc:
        raise argparse.ArgumentTypeError(f"unknown resample mode: {value}") from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build and verify item icon assets with a consistent canvas and fill ratio.")
    parser.add_argument("project_root", type=Path, nargs="?", default=Path("."))
    parser.add_argument("--source-name", default="items_icons", help="Directory under resource/source containing icon sources.")
    parser.add_argument("--icon-size", type=parse_icon_size, default=DEFAULT_ICON_SIZE)
    parser.add_argument("--fill-ratio", type=float, default=DEFAULT_FILL_RATIO)
    parser.add_argument("--resample", type=parse_resample, default=Image.Resampling.LANCZOS)
    parser.add_argument("--verify-only", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.verify_only:
        verify_item_icons(args.project_root, args.icon_size)
    else:
        build_item_icons(args.project_root, args.source_name, args.icon_size, args.fill_ratio, args.resample)
        verify_item_icons(args.project_root, args.icon_size)


if __name__ == "__main__":
    main()
