from __future__ import annotations

import argparse
import math
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

# 12x8 atlas layout, 32px per tile:
# row 0: surface dirt/stone/ores/liquids/wood/leaves used by builtin mappings
# row 1: surface sand and desert variants
# row 2: surface grass, mud, gravel, clay, and ash variants for future content
# row 3: underground cave floor and underground ore variants
# row 4: underground dry cave sand, basalt, and ruin-floor variants
# row 5: underground stone wall variants used by builtin stone mapping
# row 6: underground special floors, glow liquids, crystals, moss, lava, water
# row 7: deep mine, ruins, and dark foundation variants for future content


def tile_noise(x: int, y: int, seed: int) -> float:
    n = (x * 374761393 + y * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = (((n << 13) & 0xFFFFFFFF) ^ n) & 0xFFFFFFFF
    n = (n * ((n * n * 15731 + 789221) & 0xFFFFFFFF) + 1376312589) & 0x7FFFFFFF
    return n / 2147483647.0


def clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


def smoothstep(value: float) -> float:
    t = clamp01(value)
    return t * t * (3.0 - (2.0 * t))


def value_noise(x: float, y: float, seed: int, cell_size: float) -> float:
    cell = max(1.0, cell_size)
    gx = x / cell
    gy = y / cell
    x0 = math.floor(gx)
    y0 = math.floor(gy)
    fx = smoothstep(gx - x0)
    fy = smoothstep(gy - y0)

    n00 = tile_noise(x0, y0, seed)
    n10 = tile_noise(x0 + 1, y0, seed)
    n01 = tile_noise(x0, y0 + 1, seed)
    n11 = tile_noise(x0 + 1, y0 + 1, seed)
    nx0 = n00 + (n10 - n00) * fx
    nx1 = n01 + (n11 - n01) * fx
    return nx0 + (nx1 - nx0) * fy


def fractal_noise(x: float, y: float, seed: int, base_cell: float = 16.0, octaves: int = 3) -> float:
    total = 0.0
    amplitude = 1.0
    normalizer = 0.0
    cell = base_cell
    for octave in range(octaves):
        total += value_noise(x, y, seed + octave * 97, cell) * amplitude
        normalizer += amplitude
        amplitude *= 0.52
        cell *= 0.5
    return total / normalizer if normalizer else 0.0


def set_tile_pixel(atlas: list[RGBA], tile_x: int, tile_y: int, x: int, y: int, color: RGBA) -> None:
    px = tile_x * TILE_SIZE + x
    py = tile_y * TILE_SIZE + y
    atlas[py * ATLAS_WIDTH + px] = color


def draw_ground_tile(
    atlas: list[RGBA],
    tile_x: int,
    tile_y: int,
    base_hex: str,
    dark_hex: str,
    light_hex: str,
    seed: int,
    speckle_strength: float = 0.18,
    cell_size: float = 14.0,
    contrast: float = 0.50,
) -> None:
    base = color_from_hex(base_hex)
    dark = color_from_hex(dark_hex)
    light = color_from_hex(light_hex)
    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            large = fractal_noise(x + seed * 0.031, y + seed * 0.017, seed, cell_size, 3)
            grain = tile_noise(x, y, seed + 31)
            color = mix_color(dark, light, large)
            color = mix_color(base, color, contrast)
            if grain < speckle_strength:
                color = mix_color(color, dark, 0.22 + (grain * 0.22))
            elif grain > 1.0 - speckle_strength:
                color = mix_color(color, light, 0.18 + ((1.0 - grain) * 0.12))
            set_tile_pixel(atlas, tile_x, tile_y, x, y, color)


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
    draw_ground_tile(atlas, tile_x, tile_y, base_hex, dark_hex, light_hex, seed, speckle_strength)


def draw_cracks(
    atlas: list[RGBA],
    tile_x: int,
    tile_y: int,
    seed: int,
    crack_hex: str,
    count: int = 4,
    highlight_hex: str | None = None,
) -> None:
    crack = color_from_hex(crack_hex)
    highlight = color_from_hex(highlight_hex) if highlight_hex else None
    for path in range(count):
        x = 2 + int(tile_noise(path * 17, seed, seed + 7) * 28)
        y = 3 + int(tile_noise(seed, path * 19, seed + 13) * 26)
        length = 7 + int(tile_noise(path, seed, seed + 23) * 13)
        direction = -1 if tile_noise(path, seed, seed + 29) < 0.5 else 1
        for step in range(length):
            px = x + int(direction * step * 0.75)
            wobble = int((tile_noise(step, path, seed + 41) - 0.5) * 4)
            py = y + wobble + (step // 5)
            if 1 <= px < TILE_SIZE - 1 and 1 <= py < TILE_SIZE - 1:
                set_tile_pixel(atlas, tile_x, tile_y, px, py, crack)
                if highlight and tile_noise(px, py, seed + 43) > 0.65:
                    set_tile_pixel(atlas, tile_x, tile_y, px, py - 1, highlight)


def scatter_pebbles(
    atlas: list[RGBA],
    tile_x: int,
    tile_y: int,
    seed: int,
    colors: tuple[str, ...],
    count: int = 12,
    radius: int = 1,
) -> None:
    palette = tuple(color_from_hex(color) for color in colors)
    for idx in range(count):
        cx = 2 + int(tile_noise(idx * 11, seed, seed + 53) * 28)
        cy = 2 + int(tile_noise(seed, idx * 13, seed + 59) * 28)
        color = palette[idx % len(palette)]
        local_radius = 1 + int(tile_noise(cx, cy, seed + 61) * radius)
        for dy in range(-local_radius, local_radius + 1):
            for dx in range(-local_radius, local_radius + 1):
                px = cx + dx
                py = cy + dy
                if px < 0 or py < 0 or px >= TILE_SIZE or py >= TILE_SIZE:
                    continue
                if dx * dx + dy * dy <= local_radius * local_radius:
                    shade = 0.18 if dx + dy < 0 else -0.08
                    pixel = mix_color(color, color_from_hex("#ffffff" if shade > 0 else "#000000"), abs(shade))
                    set_tile_pixel(atlas, tile_x, tile_y, px, py, pixel)


def scatter_tufts(
    atlas: list[RGBA],
    tile_x: int,
    tile_y: int,
    seed: int,
    colors: tuple[str, ...],
    count: int = 8,
) -> None:
    palette = tuple(color_from_hex(color) for color in colors)
    for idx in range(count):
        x = 2 + int(tile_noise(idx * 23, seed, seed + 71) * 28)
        y = 3 + int(tile_noise(seed, idx * 29, seed + 73) * 26)
        color = palette[idx % len(palette)]
        set_tile_pixel(atlas, tile_x, tile_y, x, y, color)
        if y > 0:
            set_tile_pixel(atlas, tile_x, tile_y, x, y - 1, mix_color(color, color_from_hex("#d9e6a6"), 0.22))
        if x > 0 and tile_noise(x, y, seed + 79) > 0.45:
            set_tile_pixel(atlas, tile_x, tile_y, x - 1, y, mix_color(color, color_from_hex("#18200f"), 0.18))
        if x < TILE_SIZE - 1 and tile_noise(x, y, seed + 83) > 0.55:
            set_tile_pixel(atlas, tile_x, tile_y, x + 1, y, color)


def draw_ore_tile(
    atlas: list[RGBA],
    tile_x: int,
    tile_y: int,
    seed: int,
    ore_hex: str,
    host_base: str = "#686a6c",
    host_dark: str = "#4c4d50",
    host_light: str = "#8c8d90",
) -> None:
    draw_ground_tile(atlas, tile_x, tile_y, host_base, host_dark, host_light, seed, 0.13, 12.0, 0.62)
    draw_cracks(atlas, tile_x, tile_y, seed + 5, host_dark, 3, host_light)
    ore = color_from_hex(ore_hex)
    dark_ore = mix_color(ore, color_from_hex("#141414"), 0.25)
    bright_ore = mix_color(ore, color_from_hex("#fff0c8"), 0.25)
    for cluster in range(7):
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
                    color = bright_ore if dist == 0 else mix_color(ore, dark_ore, dist / 3.0)
                    set_tile_pixel(atlas, tile_x, tile_y, px, py, color)


def draw_liquid_tile(
    atlas: list[RGBA],
    tile_x: int,
    tile_y: int,
    base_hex: str,
    wave_hex: str,
    seed: int,
    foam_hex: str | None = None,
) -> None:
    base = color_from_hex(base_hex)
    wave = color_from_hex(wave_hex)
    foam = color_from_hex(foam_hex) if foam_hex else wave
    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            swirl = fractal_noise(x + seed * 0.013, y + seed * 0.019, seed, 12.0, 3)
            line = int((x * 0.9) + (y * 1.7) + swirl * 7 + seed) % 13 == 0
            n = tile_noise(x, y, seed)
            color = mix_color(base, wave, 0.18 + swirl * 0.16)
            if line or n > 0.965:
                color = mix_color(color, foam, 0.46)
            set_tile_pixel(atlas, tile_x, tile_y, x, y, color)


def draw_wood_tile(atlas: list[RGBA], tile_x: int, tile_y: int, seed: int) -> None:
    base = color_from_hex("#7c5734")
    dark = color_from_hex("#3e2a1d")
    light = color_from_hex("#b5824f")
    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            bands = (math.sin((x + seed * 0.17) * 0.68 + value_noise(x, y, seed, 9.0) * 3.0) + 1.0) * 0.5
            color = mix_color(dark, light, bands)
            color = mix_color(base, color, 0.58)
            if (x + seed) % 8 == 0:
                color = mix_color(color, dark, 0.20)
            set_tile_pixel(atlas, tile_x, tile_y, x, y, color)
    draw_cracks(atlas, tile_x, tile_y, seed + 3, "#2f2117", 5, "#c59663")


def draw_leaves_tile(atlas: list[RGBA], tile_x: int, tile_y: int, seed: int) -> None:
    draw_ground_tile(atlas, tile_x, tile_y, "#627338", "#2e3d24", "#9ba85c", seed, 0.22, 8.0, 0.78)
    scatter_tufts(atlas, tile_x, tile_y, seed + 1, ("#91aa55", "#6a8c43", "#b1b96a"), 18)
    scatter_pebbles(atlas, tile_x, tile_y, seed + 2, ("#384c28", "#7b944d"), 8, 1)


def draw_ruin_floor(atlas: list[RGBA], tile_x: int, tile_y: int, seed: int, base_hex: str) -> None:
    draw_ground_tile(atlas, tile_x, tile_y, base_hex, "#242326", "#69666b", seed, 0.10, 18.0, 0.40)
    seam = color_from_hex("#171719")
    light = color_from_hex("#77747b")
    offset_x = int(tile_noise(seed, tile_x, seed + 101) * 8)
    offset_y = int(tile_noise(tile_y, seed, seed + 103) * 8)
    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            local_x = (x + offset_x) % 16
            local_y = (y + offset_y) % 16
            if local_x == 0 or local_y == 0:
                set_tile_pixel(atlas, tile_x, tile_y, x, y, seam)
            elif local_x == 1 or local_y == 1:
                set_tile_pixel(atlas, tile_x, tile_y, x, y, mix_color(light, color_from_hex(base_hex), 0.55))
    draw_cracks(atlas, tile_x, tile_y, seed + 8, "#101014", 4, "#858187")


def draw_crystal_floor(atlas: list[RGBA], tile_x: int, tile_y: int, seed: int, base_hex: str, glow_hex: str) -> None:
    draw_ground_tile(atlas, tile_x, tile_y, base_hex, "#1c2028", "#4b4d62", seed, 0.15, 10.0, 0.62)
    glow = color_from_hex(glow_hex)
    bright = mix_color(glow, color_from_hex("#ffffff"), 0.34)
    for shard in range(7):
        cx = 3 + int(tile_noise(shard * 37, seed, seed + 89) * 25)
        cy = 4 + int(tile_noise(seed, shard * 41, seed + 91) * 23)
        height = 3 + int(tile_noise(cx, cy, seed + 93) * 5)
        for step in range(height):
            px = cx + (step // 3)
            py = cy - step
            if 0 <= px < TILE_SIZE and 0 <= py < TILE_SIZE:
                set_tile_pixel(atlas, tile_x, tile_y, px, py, bright if step == height - 1 else glow)
                if px + 1 < TILE_SIZE and tile_noise(px, py, seed + 97) > 0.55:
                    set_tile_pixel(atlas, tile_x, tile_y, px + 1, py, mix_color(glow, color_from_hex("#05060a"), 0.20))


def draw_stone_wall_tile(atlas: list[RGBA], tile_x: int, tile_y: int, seed: int) -> None:
    draw_ground_tile(atlas, tile_x, tile_y, "#4e4944", "#2b2927", "#777068", seed, 0.12, 11.0, 0.70)
    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            cell = fractal_noise(x + seed, y - seed, seed + 111, 7.0, 2)
            if cell > 0.78 and tile_noise(x, y, seed + 113) > 0.25:
                color = mix_color(color_from_hex("#777068"), color_from_hex("#a39a8f"), 0.28)
                set_tile_pixel(atlas, tile_x, tile_y, x, y, color)
            elif cell < 0.18 and tile_noise(x, y, seed + 117) > 0.40:
                set_tile_pixel(atlas, tile_x, tile_y, x, y, color_from_hex("#242220"))
    draw_cracks(atlas, tile_x, tile_y, seed + 10, "#1b1918", 5, "#887e72")


def draw_surface_dirt(atlas: list[RGBA], tile_x: int, tile_y: int, seed: int) -> None:
    draw_ground_tile(atlas, tile_x, tile_y, "#715035", "#402c20", "#a8784c", seed, 0.14, 14.0, 0.56)
    scatter_pebbles(atlas, tile_x, tile_y, seed, ("#5c4633", "#8e6a49", "#b18a5f"), 8, 1)
    if seed % 2 == 0:
        scatter_tufts(atlas, tile_x, tile_y, seed, ("#536832", "#6e7d3b"), 5)
    if seed % 3 == 0:
        draw_cracks(atlas, tile_x, tile_y, seed, "#38261d", 2, "#ad7d52")


def draw_surface_sand(atlas: list[RGBA], tile_x: int, tile_y: int, seed: int, variant: int) -> None:
    palettes = (
        ("#c7ad67", "#927a47", "#ead38b"),
        ("#bfa35e", "#80683f", "#e5cb83"),
        ("#d2ba75", "#9c7f4c", "#f0df9b"),
        ("#b99258", "#70543a", "#d9b36e"),
    )
    base, dark, light = palettes[variant % len(palettes)]
    draw_ground_tile(atlas, tile_x, tile_y, base, dark, light, seed, 0.13, 18.0, 0.50)
    if variant % 3 == 0:
        draw_cracks(atlas, tile_x, tile_y, seed + 2, "#766243", 3, "#eadc9f")
    if variant % 3 == 1:
        scatter_pebbles(atlas, tile_x, tile_y, seed + 3, ("#7c6848", "#d8c080", "#6e5b42"), 12, 1)
    if variant % 3 == 2:
        for y in range(4, TILE_SIZE, 7):
            for x in range(TILE_SIZE):
                if (x + y + seed) % 3 == 0:
                    color = color_from_hex("#e7d28b") if (x + seed) % 5 else color_from_hex("#9c814e")
                    set_tile_pixel(atlas, tile_x, tile_y, x, y + int(math.sin((x + seed) * 0.35)), color)


def draw_surface_biome_tile(atlas: list[RGBA], tile_x: int, tile_y: int, seed: int, kind: int) -> None:
    if kind < 3:
        draw_ground_tile(atlas, tile_x, tile_y, "#536d3f", "#2c422b", "#8da35d", seed, 0.18, 10.0, 0.72)
        scatter_tufts(atlas, tile_x, tile_y, seed, ("#6f8a45", "#99aa60", "#40562f"), 13)
    elif kind < 5:
        draw_ground_tile(atlas, tile_x, tile_y, "#7b713f", "#4c4529", "#b5a660", seed, 0.16, 12.0, 0.62)
        scatter_tufts(atlas, tile_x, tile_y, seed, ("#a99d55", "#695f33"), 8)
        draw_cracks(atlas, tile_x, tile_y, seed, "#423a24", 2, "#c0b26a")
    elif kind < 7:
        draw_ground_tile(atlas, tile_x, tile_y, "#554337", "#302820", "#786151", seed, 0.20, 9.0, 0.68)
        scatter_pebbles(atlas, tile_x, tile_y, seed, ("#2a2926", "#766251"), 6, 1)
    elif kind < 9:
        draw_ground_tile(atlas, tile_x, tile_y, "#746e62", "#494744", "#aaa396", seed, 0.15, 8.0, 0.76)
        scatter_pebbles(atlas, tile_x, tile_y, seed, ("#363737", "#8f8b83", "#b7b0a6"), 18, 2)
    elif kind < 11:
        draw_ground_tile(atlas, tile_x, tile_y, "#846246", "#4e392d", "#b9855b", seed, 0.14, 13.0, 0.55)
        draw_cracks(atlas, tile_x, tile_y, seed, "#3a2a23", 4, "#c28d64")
    else:
        draw_ground_tile(atlas, tile_x, tile_y, "#3b3d3d", "#222424", "#5c6060", seed, 0.16, 11.0, 0.62)
        scatter_pebbles(atlas, tile_x, tile_y, seed, ("#1a1b1b", "#6f7270"), 10, 1)


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

    # Surface: industrial-wilderness ground readability, not a direct copy of any source game.
    for i in range(4):
        draw_surface_dirt(atlas, i, 0, 100 + i)
    draw_ground_tile(atlas, 4, 0, "#6b706d", "#3f4545", "#969d96", 120, 0.13, 10.0, 0.68)
    draw_cracks(atlas, 4, 0, 120, "#333838", 4, "#a4aaa2")
    draw_ore_tile(atlas, 5, 0, 121, "#c9a06a", "#666b69", "#3d4344", "#989e98")
    draw_ore_tile(atlas, 6, 0, 122, "#d88442", "#666b69", "#3d4344", "#989e98")
    draw_ore_tile(atlas, 7, 0, 123, "#202022", "#666b69", "#3d4344", "#989e98")
    draw_liquid_tile(atlas, 8, 0, "#2e6c93", "#86cae0", 124, "#cdf5ff")
    draw_liquid_tile(atlas, 9, 0, "#b2381e", "#ffad36", 125, "#ffe169")
    draw_wood_tile(atlas, 10, 0, 126)
    draw_leaves_tile(atlas, 11, 0, 127)

    sand_files = (
        "sand_tile_01_32.png",
        "sand_tile_02_pebbles_32.png",
        "sand_tile_03_ripples_32.png",
        "sand_tile_04_alien_flecks_32.png",
    )
    for i, filename in enumerate(sand_files):
        copied = copy_tile_image(atlas, sand_dir / filename, i, 1)
        if not copied:
            draw_surface_sand(atlas, i, 1, 200 + i, i)
    for i in range(4, 12):
        draw_surface_sand(atlas, i, 1, 200 + i, i)

    for i in range(12):
        draw_surface_biome_tile(atlas, i, 2, 300 + i, i)

    # Underground: compact cave materials with warm stone, moss, crystal, and ruins.
    for i in range(3):
        draw_ground_tile(atlas, i, 3, "#51463c", "#2e2926", "#7b6d5d", 400 + i, 0.16, 9.0, 0.72)
        scatter_pebbles(atlas, i, 3, 400 + i, ("#302b27", "#786a5c", "#968777"), 8, 1)
    draw_ground_tile(atlas, 3, 3, "#5e625f", "#393e3d", "#848982", 403, 0.13, 9.0, 0.75)
    draw_cracks(atlas, 3, 3, 403, "#252929", 5, "#959b92")
    draw_ore_tile(atlas, 4, 3, 404, "#d1a15d", "#57524b", "#302e2d", "#827b70")
    draw_ore_tile(atlas, 5, 3, 405, "#d88442", "#57524b", "#302e2d", "#827b70")
    draw_ore_tile(atlas, 6, 3, 406, "#202022", "#57524b", "#302e2d", "#827b70")
    for i in range(7, 12):
        draw_ground_tile(atlas, i, 3, "#463d36", "#28231f", "#6d5d51", 407 + i, 0.18, 8.0, 0.72)
        if i % 2 == 0:
            scatter_tufts(atlas, i, 3, 407 + i, ("#476245", "#6e8e65"), 8)

    for i in range(6):
        draw_ground_tile(atlas, i, 4, "#4b433c", "#2b2724", "#71655a", 500 + i, 0.17, 10.0, 0.70)
        draw_cracks(atlas, i, 4, 500 + i, "#211f1d", 2, "#82756a")
    for i in range(6, 10):
        draw_surface_sand(atlas, i, 4, 500 + i, i)
    for i in range(10, 12):
        draw_ruin_floor(atlas, i, 4, 500 + i, "#39353c")

    for i in range(12):
        draw_stone_wall_tile(atlas, i, 5, 600 + i)
    for i in range(12):
        if i < 3:
            draw_crystal_floor(atlas, i, 6, 700 + i, "#34313d", ("#75d1ff", "#b885ff", "#ffd16a")[i])
        elif i < 5:
            draw_ground_tile(atlas, i, 6, "#334235", "#1e2923", "#66805d", 700 + i, 0.18, 8.0, 0.74)
            scatter_tufts(atlas, i, 6, 700 + i, ("#65a260", "#98d079", "#3d6042"), 14)
        elif i < 7:
            draw_ground_tile(atlas, i, 6, "#34313b", "#1f1d28", "#5f5870", 700 + i, 0.17, 9.0, 0.70)
            scatter_pebbles(atlas, i, 6, 700 + i, ("#6d5a86", "#312945", "#8b7caf"), 10, 1)
        else:
            draw_ground_tile(atlas, i, 6, "#35313b", "#201d28", "#5f5870", 700 + i, 0.16, 9.0, 0.62)
    draw_liquid_tile(atlas, 7, 6, "#963718", "#ff9a2e", 777, "#ffd362")
    draw_liquid_tile(atlas, 8, 6, "#234a61", "#70cbe8", 778, "#bdf4ff")
    draw_crystal_floor(atlas, 9, 6, 779, "#2f2c39", "#9dffcf")
    draw_ruin_floor(atlas, 10, 6, 780, "#302d34")
    draw_ruin_floor(atlas, 11, 6, 781, "#292b32")

    for i in range(12):
        if i < 4:
            draw_ruin_floor(atlas, i, 7, 800 + i, "#2c2f32")
        elif i < 8:
            draw_ground_tile(atlas, i, 7, "#25272b", "#15171a", "#444950", 800 + i, 0.14, 8.0, 0.76)
            draw_cracks(atlas, i, 7, 800 + i, "#101113", 4, "#54595d")
        else:
            draw_ground_tile(atlas, i, 7, "#20272a", "#111719", "#3d5152", 800 + i, 0.16, 9.0, 0.70)
            scatter_pebbles(atlas, i, 7, 800 + i, ("#334b4c", "#182325", "#657b78"), 12, 1)

    output = terrain_dir / "voxel_material_atlas_32.png"
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


def build_assets(project_root: Path, build_terrain: bool = True, build_items: bool = True) -> None:
    project_root = project_root.resolve()
    if build_terrain:
        build_terrain_atlas(project_root)
    if build_items:
        build_stone_tiny_dust_icon(project_root)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build prototype terrain atlas and terrain drop icons.")
    parser.add_argument("project_root", type=Path, nargs="?", default=Path("."))
    parser.add_argument(
        "--terrain-only",
        action="store_true",
        help="Only rebuild resource/terrain/voxel_material_atlas_32.png.",
    )
    parser.add_argument(
        "--items-only",
        action="store_true",
        help="Only rebuild terrain-related item icons.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.terrain_only and args.items_only:
        raise SystemExit("--terrain-only and --items-only cannot be used together.")
    build_assets(
        args.project_root,
        build_terrain=not args.items_only,
        build_items=not args.terrain_only,
    )


if __name__ == "__main__":
    main()
