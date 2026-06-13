from __future__ import annotations

import argparse
from pathlib import Path

from png_pixel_tools import RGBA, paste, write_png


WIDTH = 32
HEIGHT = 48
FRAME_COUNT = 4

TRANSPARENT: RGBA = (0, 0, 0, 0)
OUTLINE: RGBA = (25, 24, 28, 255)
SHADOW: RGBA = (0, 0, 0, 70)
SKIN: RGBA = (210, 154, 104, 255)
HAIR: RGBA = (62, 43, 35, 255)
SHIRT: RGBA = (58, 132, 145, 255)
SHIRT_DARK: RGBA = (35, 86, 98, 255)
PANTS: RGBA = (51, 65, 98, 255)
BOOT: RGBA = (39, 34, 32, 255)
HIGHLIGHT: RGBA = (128, 210, 211, 255)


def image(width: int = WIDTH, height: int = HEIGHT) -> list[RGBA]:
    return [TRANSPARENT for _ in range(width * height)]


def set_pixel(pixels: list[RGBA], x: int, y: int, color: RGBA) -> None:
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        pixels[y * WIDTH + x] = color


def rect(pixels: list[RGBA], x0: int, y0: int, x1: int, y1: int, color: RGBA) -> None:
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            set_pixel(pixels, x, y, color)


def ellipse(pixels: list[RGBA], cx: int, cy: int, rx: int, ry: int, color: RGBA) -> None:
    rx2 = max(1, rx * rx)
    ry2 = max(1, ry * ry)
    for y in range(cy - ry, cy + ry + 1):
        for x in range(cx - rx, cx + rx + 1):
            if ((x - cx) * (x - cx) * ry2) + ((y - cy) * (y - cy) * rx2) <= rx2 * ry2:
                set_pixel(pixels, x, y, color)


def draw_body(pixels: list[RGBA], facing: str, step: int) -> None:
    sway = [0, 1, 0, -1][step % FRAME_COUNT]
    bob = [0, 0, 1, 0][step % FRAME_COUNT]
    arm = [0, 1, 0, -1][step % FRAME_COUNT]
    leg = [-1, 1, 1, -1][step % FRAME_COUNT]

    ellipse(pixels, 16, 43, 8, 3, SHADOW)

    rect(pixels, 10 + leg, 28 + bob, 14 + leg, 38 + bob, OUTLINE)
    rect(pixels, 18 - leg, 28 + bob, 22 - leg, 38 + bob, OUTLINE)
    rect(pixels, 11 + leg, 28 + bob, 13 + leg, 37 + bob, PANTS)
    rect(pixels, 19 - leg, 28 + bob, 21 - leg, 37 + bob, PANTS)
    rect(pixels, 9 + leg, 38 + bob, 14 + leg, 40 + bob, BOOT)
    rect(pixels, 18 - leg, 38 + bob, 23 - leg, 40 + bob, BOOT)

    rect(pixels, 9 + sway, 17 + bob, 23 + sway, 29 + bob, OUTLINE)
    rect(pixels, 11 + sway, 18 + bob, 21 + sway, 28 + bob, SHIRT)
    rect(pixels, 12 + sway, 18 + bob, 20 + sway, 20 + bob, HIGHLIGHT)
    rect(pixels, 11 + sway, 26 + bob, 21 + sway, 28 + bob, SHIRT_DARK)

    rect(pixels, 6 + arm, 18 + bob, 10 + arm, 30 + bob, OUTLINE)
    rect(pixels, 22 - arm, 18 + bob, 26 - arm, 30 + bob, OUTLINE)
    rect(pixels, 7 + arm, 20 + bob, 9 + arm, 28 + bob, SHIRT_DARK)
    rect(pixels, 23 - arm, 20 + bob, 25 - arm, 28 + bob, SHIRT_DARK)

    ellipse(pixels, 16 + sway, 10 + bob, 8, 8, OUTLINE)
    ellipse(pixels, 16 + sway, 11 + bob, 6, 6, SKIN)
    rect(pixels, 10 + sway, 4 + bob, 22 + sway, 9 + bob, HAIR)

    if facing == "up":
        rect(pixels, 10 + sway, 6 + bob, 22 + sway, 13 + bob, HAIR)
        rect(pixels, 12 + sway, 14 + bob, 20 + sway, 15 + bob, SKIN)
    elif facing == "side":
        rect(pixels, 11 + sway, 5 + bob, 16 + sway, 13 + bob, HAIR)
        rect(pixels, 19 + sway, 10 + bob, 21 + sway, 12 + bob, SKIN)
        set_pixel(pixels, 20 + sway, 10 + bob, OUTLINE)
    else:
        rect(pixels, 10 + sway, 4 + bob, 22 + sway, 8 + bob, HAIR)
        set_pixel(pixels, 13 + sway, 11 + bob, OUTLINE)
        set_pixel(pixels, 19 + sway, 11 + bob, OUTLINE)
        rect(pixels, 14 + sway, 15 + bob, 18 + sway, 16 + bob, OUTLINE)


def frame(facing: str, step: int = 0) -> list[RGBA]:
    pixels = image()
    draw_body(pixels, facing, step)
    return pixels


def write_strip(path: Path, facing: str) -> None:
    strip_width = WIDTH * FRAME_COUNT
    strip = [TRANSPARENT for _ in range(strip_width * HEIGHT)]
    for step in range(FRAME_COUNT):
        paste(strip, strip_width, HEIGHT, frame(facing, step), WIDTH, HEIGHT, step * WIDTH, 0)
    write_png(path, strip_width, HEIGHT, strip)


def build_assets(output_directory: Path) -> None:
    output_directory.mkdir(parents=True, exist_ok=True)

    for facing in ("down", "side", "up"):
        write_png(output_directory / f"player_idle_{facing}_32x48.png", WIDTH, HEIGHT, frame(facing, 0))
        write_strip(output_directory / f"player_walk_{facing}_32x48_strip.png", facing)

    print(f"[assets] Wrote placeholder player sprites to '{output_directory.resolve()}'.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build placeholder 32x48 player sprites.")
    parser.add_argument("project_root", type=Path, nargs="?", default=Path("."))
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    build_assets(args.project_root.resolve() / "resource" / "characters" / "player")


if __name__ == "__main__":
    main()
