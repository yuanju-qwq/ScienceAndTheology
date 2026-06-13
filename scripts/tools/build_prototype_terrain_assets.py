from __future__ import annotations

import argparse
from pathlib import Path

from png_pixel_tools import (
    RGBA,
    color_from_hex,
    mix_color,
    new_image,
    paste,
    read_png,
    resize_nearest,
    write_png,
)


TILE_SIZE = 32
ATLAS_WIDTH = 12 * TILE_SIZE
ATLAS_HEIGHT = 8 * TILE_SIZE


def tile_noise(x: int, y: int, seed: int) -> float:
    n = (x * 374761393 + y * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = (((n << 13) & 0xFFFFFFFF) ^ n) & 0xFFFFFFFF
    n = (n * ((n * n * 15731 + 789221) & 0xFFFFFFFF) + 1376312589) & 0x7FFFFFFF
    return n / 2147483647.0


def set_tile_pixel(atlas: list[RGBA], tile_x: int, tile_y: int, x: int, y: int, color: RGBA) -> None:
    px = tile_x * TILE_SIZE + x
    py = tile_y * TILE_SIZE + y
    atlas[py * ATLAS_WIDTH + px] = color


def draw_noise_tile(
    atlas: list[RGBA],
    tile_x: int,
    tile_y: int,
    base_hex: str,
    dark_hex: str,
    light_hex: str,
    seed: int,
    speckle_strength: float = 0.18,
) -> None:
    base = color_from_hex(base_hex)
    dark = color_from_hex(dark_hex)
    light = color_from_hex(light_hex)
    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            n = tile_noise(x, y, seed)
            grain = tile_noise(x // 2, y // 2, seed + 31)
            color = base
            if n < speckle_strength:
                color = mix_color(base, dark, 0.30 + (grain * 0.30))
            elif n > 1.0 - speckle_strength:
                color = mix_color(base, light, 0.22 + (grain * 0.25))
            if (x + y + seed) % 13 == 0:
                color = mix_color(color, light, 0.14)
            set_tile_pixel(atlas, tile_x, tile_y, x, y, color)


def draw_ore_tile(atlas: list[RGBA], tile_x: int, tile_y: int, seed: int, ore_hex: str) -> None:
    draw_noise_tile(atlas, tile_x, tile_y, "#686a6c", "#4c4d50", "#8c8d90", seed, 0.16)
    ore = color_from_hex(ore_hex)
    dark_ore = mix_color(ore, color_from_hex("#141414"), 0.25)
    for cluster in range(6):
        cx = 4 + ((seed * 7 + cluster * 11) % 24)
        cy = 4 + ((seed * 13 + cluster * 5) % 24)
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                px = cx + dx
                py = cy + dy
                if px < 1 or py < 1 or px > 30 or py > 30:
                    continue
                dist = abs(dx) + abs(dy)
                if dist <= 2 and tile_noise(px, py, seed + cluster) > 0.24:
                    color = ore if dist == 0 else mix_color(ore, dark_ore, dist / 3.0)
                    set_tile_pixel(atlas, tile_x, tile_y, px, py, color)


def draw_liquid_tile(atlas: list[RGBA], tile_x: int, tile_y: int, base_hex: str, wave_hex: str, seed: int) -> None:
    base = color_from_hex(base_hex)
    wave = color_from_hex(wave_hex)
    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            line = ((x + (y * 2) + seed) % 11) == 0
            n = tile_noise(x, y, seed)
            color = mix_color(base, wave, 0.45) if line or n > 0.94 else base
            set_tile_pixel(atlas, tile_x, tile_y, x, y, color)


def draw_rim(atlas: list[RGBA], tile_x: int, tile_y: int, hex_color: str) -> None:
    rim = color_from_hex(hex_color)
    for i in range(TILE_SIZE):
        set_tile_pixel(atlas, tile_x, tile_y, i, 0, rim)
        set_tile_pixel(atlas, tile_x, tile_y, i, TILE_SIZE - 1, rim)
        set_tile_pixel(atlas, tile_x, tile_y, 0, i, rim)
        set_tile_pixel(atlas, tile_x, tile_y, TILE_SIZE - 1, i, rim)


def copy_tile_image(atlas: list[RGBA], path: Path, tile_x: int, tile_y: int) -> bool:
    if not path.exists():
        return False
    width, height, pixels = read_png(path)
    resized = resize_nearest(pixels, width, height, TILE_SIZE, TILE_SIZE)
    paste(atlas, ATLAS_WIDTH, ATLAS_HEIGHT, resized, TILE_SIZE, TILE_SIZE, tile_x * TILE_SIZE, tile_y * TILE_SIZE)
    return True


def build_terrain_atlas(project_root: Path) -> None:
    terrain_dir = project_root / "resource" / "terrain"
    sand_dir = terrain_dir / "sand"
    atlas = new_image(ATLAS_WIDTH, ATLAS_HEIGHT)

    for i in range(4):
        draw_noise_tile(atlas, i, 0, "#765231", "#4f3521", "#9a7040", 100 + i, 0.20)
    draw_noise_tile(atlas, 4, 0, "#6f7274", "#4d4f52", "#929597", 120, 0.18)
    draw_ore_tile(atlas, 5, 0, 121, "#c9a06a")
    draw_ore_tile(atlas, 6, 0, 122, "#d88442")
    draw_ore_tile(atlas, 7, 0, 123, "#202022")
    draw_liquid_tile(atlas, 8, 0, "#2f74b8", "#82c7e8", 124)
    draw_liquid_tile(atlas, 9, 0, "#ba3d19", "#ffb13b", 125)
    draw_noise_tile(atlas, 10, 0, "#5c8b3d", "#37622c", "#8ab65a", 126, 0.20)
    draw_noise_tile(atlas, 11, 0, "#80623b", "#5b452d", "#b08a55", 127, 0.18)

    sand_files = (
        "sand_tile_01_32.png",
        "sand_tile_02_pebbles_32.png",
        "sand_tile_03_ripples_32.png",
        "sand_tile_04_alien_flecks_32.png",
    )
    for i, filename in enumerate(sand_files):
        copied = copy_tile_image(atlas, sand_dir / filename, i, 1)
        if not copied:
            draw_noise_tile(atlas, i, 1, "#c9b56f", "#a18a4f", "#ead58f", 200 + i, 0.20)
    for i in range(4, 12):
        draw_noise_tile(atlas, i, 1, "#c8af68", "#9f884e", "#ead58c", 200 + i, 0.18)

    for i in range(12):
        draw_noise_tile(atlas, i, 2, "#4f6e45", "#2f4630", "#87a45e", 300 + i, 0.22)

    for i in range(3):
        draw_noise_tile(atlas, i, 3, "#51473d", "#332d28", "#766b5c", 400 + i, 0.22)
    draw_noise_tile(atlas, 3, 3, "#5f6265", "#3e4145", "#82868a", 403, 0.18)
    draw_ore_tile(atlas, 4, 3, 404, "#c9a06a")
    draw_ore_tile(atlas, 5, 3, 405, "#d88442")
    draw_ore_tile(atlas, 6, 3, 406, "#202022")
    for i in range(7, 12):
        draw_noise_tile(atlas, i, 3, "#453b35", "#292420", "#68594f", 407 + i, 0.25)

    for i in range(6):
        draw_noise_tile(atlas, i, 4, "#4b433c", "#302b27", "#71655a", 500 + i, 0.22)
    for i in range(6, 10):
        draw_noise_tile(atlas, i, 4, "#bfa562", "#8d7749", "#e3ce8a", 500 + i, 0.20)
    for i in range(10, 12):
        draw_noise_tile(atlas, i, 4, "#37323a", "#242129", "#615a69", 500 + i, 0.22)

    for i in range(12):
        draw_noise_tile(atlas, i, 5, "#403b36", "#292622", "#696058", 600 + i, 0.22)
    for i in range(12):
        draw_noise_tile(atlas, i, 6, "#35313b", "#201d28", "#5f5870", 700 + i, 0.22)
    draw_liquid_tile(atlas, 7, 6, "#49356d", "#b885ff", 777)
    draw_liquid_tile(atlas, 8, 6, "#274f65", "#77d7ff", 778)

    for i in range(12):
        draw_noise_tile(atlas, i, 7, "#25272b", "#17191d", "#444950", 800 + i, 0.18)

    for y in range(8):
        for x in range(12):
            draw_rim(atlas, x, y, "#000000")

    output = terrain_dir / "dual_layer_tileset_32_clean.png"
    write_png(output, ATLAS_WIDTH, ATLAS_HEIGHT, atlas)
    print(f"[assets] Wrote terrain atlas '{output.resolve()}'.")


def build_stone_tiny_dust_icon(project_root: Path) -> None:
    materials_dir = project_root / "resource" / "items" / "materials"
    icon = new_image(32, 32)

    shadow = color_from_hex("#2e2f31")
    base = color_from_hex("#777a7d")
    light = color_from_hex("#b8bbbe")
    dust = (
        (8, 19, 4),
        (13, 17, 5),
        (19, 19, 4),
        (22, 15, 3),
        (10, 12, 3),
        (17, 11, 4),
        (25, 22, 2),
        (6, 23, 2),
    )
    for cx, cy, radius in dust:
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                px = cx + dx
                py = cy + dy
                if px < 0 or py < 0 or px > 31 or py > 31:
                    continue
                if (dx * dx + dy * dy) <= (radius * radius):
                    n = tile_noise(px, py, 910)
                    color = light if n > 0.72 else shadow if n < 0.22 else base
                    icon[py * 32 + px] = color

    output = materials_dir / "stone_tiny_dust_icon_32.png"
    write_png(output, 32, 32, icon)
    print(f"[assets] Wrote material icon '{output.resolve()}'.")


def build_assets(project_root: Path) -> None:
    project_root = project_root.resolve()
    build_terrain_atlas(project_root)
    build_stone_tiny_dust_icon(project_root)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build prototype terrain atlas and terrain drop icons.")
    parser.add_argument("project_root", type=Path, nargs="?", default=Path("."))
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    build_assets(args.project_root)


if __name__ == "__main__":
    main()
