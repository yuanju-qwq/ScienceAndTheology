#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import math
import random
from pathlib import Path
from typing import Iterable, Tuple

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
GT_ASSETS = ROOT.parent / "GregTech" / "src" / "main" / "resources" / "assets" / "gregtech" / "textures"

RGBA = Tuple[int, int, int, int]
RGB = Tuple[int, int, int]


def stable_seed(name: str) -> int:
    return int(hashlib.sha1(name.encode("utf-8")).hexdigest()[:8], 16)


def out_path(rel: str) -> Path:
    path = ROOT / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def save(img: Image.Image, rel: str) -> None:
    path = out_path(rel)
    img.save(path, "PNG", optimize=True)
    print(f"[OK] {path.relative_to(ROOT)}")


def gt_png(rel: str) -> Image.Image:
    path = GT_ASSETS / rel
    if not path.exists():
        raise FileNotFoundError(path)
    return Image.open(path).convert("RGBA")


def fit_nearest(img: Image.Image, size: int) -> Image.Image:
    if img.size == (size, size):
        return img.copy()
    return img.resize((size, size), Image.Resampling.NEAREST)


def normalize_tint_base(img: Image.Image, size: int = 32) -> Image.Image:
    src = fit_nearest(img.convert("RGBA"), size)
    dst = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sp = src.load()
    dp = dst.load()
    for y in range(size):
        for x in range(size):
            r, g, b, a = sp[x, y]
            if a == 0:
                continue
            luma = int(max(0, min(255, r * 0.299 + g * 0.587 + b * 0.114)))
            dp[x, y] = (luma, luma, luma, a)
    return dst


def normalize_overlay(img: Image.Image, size: int = 32) -> Image.Image:
    src = fit_nearest(img.convert("RGBA"), size)
    dst = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sp = src.load()
    dp = dst.load()
    for y in range(size):
        for x in range(size):
            r, g, b, a = sp[x, y]
            if a == 0:
                continue
            luma = int(max(0, min(255, r * 0.299 + g * 0.587 + b * 0.114)))
            alpha = int(a * (luma / 255.0))
            dp[x, y] = (255, 255, 255, alpha)
    return dst


def shade(color: RGB, factor: float) -> RGB:
    return tuple(max(0, min(255, int(c * factor))) for c in color)


def icon_canvas(size: int = 32) -> Image.Image:
    return Image.new("RGBA", (size, size), (0, 0, 0, 0))


def draw_pixel_box(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], color: RGBA, outline: RGBA) -> None:
    draw.rectangle(box, fill=color, outline=outline)
    x0, y0, x1, y1 = box
    if x1 - x0 > 5 and y1 - y0 > 5:
        draw.line((x0 + 1, y0 + 1, x1 - 1, y0 + 1), fill=(255, 255, 255, 70))
        draw.line((x0 + 1, y0 + 1, x0 + 1, y1 - 1), fill=(255, 255, 255, 45))
        draw.line((x0 + 1, y1 - 1, x1 - 1, y1 - 1), fill=(0, 0, 0, 65))


def material_lump(rel: str, color: RGB, size: int = 32, kind: str = "lump") -> None:
    img = icon_canvas(size)
    d = ImageDraw.Draw(img)
    outline = (25, 20, 16, 255)
    if kind == "stick":
        d.line((9, 24, 23, 8), fill=outline, width=5)
        d.line((10, 23, 22, 9), fill=(*color, 255), width=3)
        d.point((13, 20), fill=shade(color, 0.65))
    elif kind == "plank":
        draw_pixel_box(d, (7, 10, 24, 22), (*color, 255), outline)
        d.line((9, 14, 22, 14), fill=shade(color, 0.7), width=1)
        d.line((10, 18, 23, 18), fill=shade(color, 0.8), width=1)
    elif kind == "log":
        draw_pixel_box(d, (7, 9, 24, 23), (*shade(color, 0.75), 255), outline)
        d.ellipse((10, 11, 21, 21), fill=(*shade(color, 1.25), 255), outline=outline)
        d.ellipse((13, 14, 18, 19), outline=shade(color, 0.65))
    elif kind == "stone":
        points = [(8, 22), (6, 14), (13, 8), (23, 10), (26, 18), (20, 25)]
        d.polygon(points, fill=(*color, 255), outline=outline)
        d.line((10, 15, 18, 11), fill=(*shade(color, 1.25), 255))
        d.line((13, 22, 22, 18), fill=(*shade(color, 0.65), 255))
    elif kind == "straw":
        for x in (10, 14, 18, 22):
            d.line((x, 23, x - 4, 9), fill=outline, width=3)
            d.line((x, 23, x - 4, 9), fill=(*color, 255), width=1)
    else:
        d.ellipse((8, 8, 24, 24), fill=(*color, 255), outline=outline)
        d.ellipse((12, 10, 18, 15), fill=(*shade(color, 1.25), 210))
    save(img, rel)


def draw_tool_icon(rel: str, head: RGB, handle: RGB = (116, 74, 36), kind: str = "pickaxe") -> None:
    img = icon_canvas(32)
    d = ImageDraw.Draw(img)
    outline = (28, 22, 18, 255)
    if kind in {"pickaxe", "hoe"}:
        d.line((12, 23, 22, 13), fill=outline, width=5)
        d.line((13, 22, 21, 14), fill=(*handle, 255), width=3)
        d.line((8, 9, 25, 9), fill=outline, width=5)
        d.line((9, 9, 24, 9), fill=(*head, 255), width=3)
        d.point((12, 8), fill=(*shade(head, 1.35), 255))
    elif kind == "axe":
        d.line((13, 24, 21, 10), fill=outline, width=5)
        d.line((14, 23, 20, 11), fill=(*handle, 255), width=3)
        d.polygon([(15, 8), (25, 11), (24, 18), (15, 17)], fill=(*head, 255), outline=outline)
    elif kind == "shovel":
        d.line((11, 24, 20, 12), fill=outline, width=5)
        d.line((12, 23, 19, 13), fill=(*handle, 255), width=3)
        d.polygon([(20, 8), (26, 12), (23, 18), (17, 14)], fill=(*head, 255), outline=outline)
    elif kind == "sword":
        d.line((9, 23, 23, 9), fill=outline, width=5)
        d.line((10, 22, 22, 10), fill=(*head, 255), width=3)
        d.rectangle((7, 22, 13, 25), fill=(*handle, 255), outline=outline)
    elif kind == "wrench":
        d.line((9, 23, 22, 10), fill=outline, width=5)
        d.line((10, 22, 21, 11), fill=(*head, 255), width=3)
        d.arc((16, 5, 28, 17), 35, 300, fill=outline, width=4)
        d.arc((17, 6, 27, 16), 35, 300, fill=(*head, 255), width=2)
    elif kind == "file":
        d.rectangle((13, 7, 19, 24), fill=(*head, 255), outline=outline)
        for y in range(10, 22, 3):
            d.line((14, y, 18, y + 1), fill=shade(head, 0.6))
        d.rectangle((12, 24, 20, 27), fill=(*handle, 255), outline=outline)
    elif kind == "saw":
        d.polygon([(7, 11), (23, 8), (25, 14), (10, 20)], fill=(*head, 255), outline=outline)
        for x in range(10, 23, 3):
            d.line((x, 18, x + 1, 21), fill=outline)
        d.rectangle((7, 20, 14, 26), fill=(*handle, 255), outline=outline)
    elif kind == "crowbar":
        d.line((11, 24, 21, 8), fill=outline, width=5)
        d.line((12, 23, 20, 9), fill=(*head, 255), width=3)
        d.arc((18, 5, 27, 14), 80, 250, fill=outline, width=3)
    elif kind == "mallet":
        d.rectangle((8, 8, 22, 15), fill=(*head, 255), outline=outline)
        d.line((17, 15, 12, 26), fill=outline, width=5)
        d.line((17, 16, 13, 25), fill=(*handle, 255), width=3)
    elif kind == "wire_cutter":
        d.line((10, 23, 16, 15), fill=outline, width=4)
        d.line((22, 23, 16, 15), fill=outline, width=4)
        d.line((10, 23, 16, 15), fill=(*handle, 255), width=2)
        d.line((22, 23, 16, 15), fill=(*handle, 255), width=2)
        d.polygon([(13, 9), (18, 15), (15, 17), (10, 11)], fill=(*head, 255), outline=outline)
        d.polygon([(19, 9), (14, 15), (17, 17), (22, 11)], fill=(*head, 255), outline=outline)
    else:
        draw_pixel_box(d, (8, 8, 24, 24), (*head, 255), outline)
    save(img, rel)


def draw_component(rel: str, color: RGB, kind: str = "box") -> None:
    img = icon_canvas(32)
    d = ImageDraw.Draw(img)
    outline = (25, 22, 20, 255)
    if kind == "circuit":
        draw_pixel_box(d, (6, 8, 25, 23), (*color, 255), outline)
        for x in (10, 15, 20):
            d.rectangle((x, 12, x + 2, 14), fill=(230, 210, 90, 255))
        d.line((9, 18, 23, 18), fill=(70, 230, 180, 255))
    elif kind == "cell":
        d.rectangle((11, 6, 21, 25), fill=(*color, 255), outline=outline)
        d.rectangle((13, 4, 19, 7), fill=(*shade(color, 1.15), 255), outline=outline)
        d.line((13, 11, 19, 11), fill=(255, 255, 255, 80))
    elif kind == "motor":
        d.ellipse((6, 9, 24, 24), fill=(*color, 255), outline=outline)
        d.rectangle((21, 13, 27, 19), fill=(165, 120, 70, 255), outline=outline)
        d.ellipse((12, 14, 18, 20), fill=(40, 45, 50, 255))
    elif kind == "conveyor":
        d.rounded_rectangle((5, 10, 26, 22), radius=2, fill=(42, 43, 42, 255), outline=outline)
        for x in range(8, 24, 4):
            d.line((x, 11, x - 4, 21), fill=(*color, 255))
    elif kind == "blueprint":
        draw_pixel_box(d, (7, 6, 24, 25), (*color, 255), outline)
        d.line((10, 11, 21, 11), fill=(210, 235, 255, 255))
        d.line((10, 16, 18, 16), fill=(210, 235, 255, 255))
        d.rectangle((15, 18, 21, 22), outline=(210, 235, 255, 255))
    else:
        draw_pixel_box(d, (7, 8, 24, 24), (*color, 255), outline)
        d.rectangle((11, 12, 20, 20), outline=(*shade(color, 0.55), 255))
    save(img, rel)


def draw_tree_icon(rel: str, color: RGB, kind: str, size: int = 64) -> None:
    scale = size / 32.0
    img = icon_canvas(size)
    d = ImageDraw.Draw(img)
    def s(v: int) -> int:
        return int(v * scale)
    outline = (25, 20, 16, 255)
    if kind == "log":
        d.rectangle((s(7), s(8), s(24), s(24)), fill=(*shade(color, 0.8), 255), outline=outline)
        d.ellipse((s(10), s(10), s(21), s(22)), fill=(*shade(color, 1.22), 255), outline=outline)
    elif kind == "plank":
        d.rectangle((s(6), s(10), s(25), s(22)), fill=(*color, 255), outline=outline)
        for y in (13, 17):
            d.line((s(8), s(y), s(23), s(y)), fill=(*shade(color, 0.72), 255), width=max(1, s(1)))
    elif kind == "fruit":
        d.ellipse((s(10), s(10), s(22), s(24)), fill=(*color, 255), outline=outline)
        d.line((s(16), s(10), s(19), s(6)), fill=(70, 100, 40, 255), width=max(1, s(2)))
    else:
        d.rectangle((s(15), s(15), s(18), s(25)), fill=(95, 55, 25, 255), outline=outline)
        d.polygon([(s(16), s(7)), (s(7), s(18)), (s(25), s(18))],
                  fill=(*color, 255), outline=outline)
        d.polygon([(s(16), s(11)), (s(9), s(23)), (s(24), s(23))],
                  fill=(*shade(color, 0.85), 255), outline=outline)
    save(img, rel)


def make_tile(rel: str, seed_name: str, kind: str = "stone") -> None:
    size = 32
    rng = random.Random(stable_seed(seed_name))
    img = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    px = img.load()
    for y in range(size):
        for x in range(size):
            n = rng.randint(-18, 18)
            wave = int(math.sin((x + y * 0.35 + rng.random()) * 0.65) * 8)
            base = 190 + n + wave
            if kind in {"deepstone", "charcoal", "barrier"}:
                base -= 70
            if kind in {"snow", "ice", "water"}:
                base += 35
            if kind == "lava":
                base = 210 + rng.randint(-12, 24)
            base = max(45, min(245, base))
            alpha = 210 if kind in {"water", "ice"} else 255
            px[x, y] = (base, base, base, alpha)
    d = ImageDraw.Draw(img)
    if kind == "ore":
        for _ in range(9):
            x = rng.randint(3, 27)
            y = rng.randint(3, 27)
            r = rng.randint(2, 4)
            d.ellipse((x - r, y - r, x + r, y + r), fill=(245, 245, 245, 255), outline=(95, 95, 95, 255))
    elif kind in {"wood", "plank"}:
        for y in range(5, 31, 7):
            d.line((0, y, 31, y + rng.randint(-1, 1)), fill=(120, 120, 120, 255))
    elif kind == "log_top":
        d.ellipse((5, 5, 27, 27), outline=(90, 90, 90, 255), width=2)
        d.ellipse((11, 11, 21, 21), outline=(115, 115, 115, 255), width=1)
    elif kind == "leaves":
        for _ in range(30):
            x = rng.randint(0, 31)
            y = rng.randint(0, 31)
            d.rectangle((x, y, min(31, x + 2), min(31, y + 1)), fill=(210, 210, 210, 255))
    elif kind == "crop":
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        for x in (10, 15, 20):
            d.line((x, 27, x + rng.randint(-5, 5), 8), fill=(235, 235, 235, 255), width=3)
            d.line((x, 27, x + rng.randint(-4, 4), 13), fill=(145, 145, 145, 255), width=1)
    elif kind == "utility":
        d.rectangle((4, 4, 27, 27), outline=(80, 80, 80, 255), width=2)
        d.rectangle((9, 9, 22, 22), outline=(150, 150, 150, 255), width=1)
    save(img, rel)


def generate_material_bases() -> None:
    mappings = {
        "resource/items/material_sets/generic/dust_base_32.png": "items/material_sets/dull/dust.png",
        "resource/items/material_sets/generic/dust_tiny_base_32.png": "items/material_sets/dull/dust_tiny.png",
        "resource/items/material_sets/generic/crushed_base_32.png": "items/material_sets/dull/crushed.png",
        "resource/items/material_sets/generic/ingot_base_32.png": "items/material_sets/shiny/ingot.png",
        "resource/items/material_sets/generic/gem_base_32.png": "items/material_sets/dull/gem.png",
    }
    for rel, gt_rel in mappings.items():
        save(normalize_tint_base(gt_png(gt_rel)), rel)
    save(normalize_overlay(gt_png("items/material_sets/shiny/ingot_overlay.png")),
         "resource/items/material_sets/generic/ingot_overlay_32.png")


def generate_fallback_icons() -> None:
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle((4, 4, 27, 27), fill=(44, 42, 48, 255), outline=(190, 182, 160, 255), width=2)
    d.line((9, 9, 22, 22), fill=(230, 225, 210, 255), width=3)
    d.line((22, 9, 9, 22), fill=(230, 225, 210, 255), width=3)
    d.rectangle((12, 12, 19, 19), outline=(110, 104, 96, 255))
    save(img, "resource/items/missing_icon_32.png")


def generate_material_icons() -> None:
    material_lump("resource/items/materials/wood_log_icon_32.png", (140, 92, 48), kind="log")
    material_lump("resource/items/materials/wood_plank_icon_32.png", (176, 125, 63), kind="plank")
    material_lump("resource/items/materials/stick_icon_32.png", (150, 92, 45), kind="stick")
    material_lump("resource/items/materials/flint_icon_32.png", (72, 64, 54), kind="stone")
    material_lump("resource/items/materials/chert_icon_32.png", (92, 84, 68), kind="stone")


def generate_tool_icons() -> None:
    tool_specs = [
        ("wooden_pickaxe", (142, 92, 44), "pickaxe"), ("stone_pickaxe", (145, 145, 145), "pickaxe"),
        ("iron_pickaxe", (190, 194, 202), "pickaxe"), ("wooden_axe", (142, 92, 44), "axe"),
        ("stone_axe", (145, 145, 145), "axe"), ("iron_axe", (190, 194, 202), "axe"),
        ("wooden_shovel", (142, 92, 44), "shovel"), ("stone_shovel", (145, 145, 145), "shovel"),
        ("iron_shovel", (190, 194, 202), "shovel"), ("wooden_sword", (142, 92, 44), "sword"),
        ("stone_sword", (145, 145, 145), "sword"), ("iron_sword", (190, 194, 202), "sword"),
        ("stone_hoe", (145, 145, 145), "hoe"), ("stone_knife", (145, 145, 145), "sword"),
        ("gt_hammer", (170, 172, 178), "mallet"), ("gt_wrench", (160, 166, 170), "wrench"),
        ("gt_file", (145, 145, 150), "file"), ("gt_screwdriver", (180, 105, 45), "sword"),
        ("gt_saw", (172, 172, 168), "saw"), ("gt_wire_cutter", (150, 155, 160), "wire_cutter"),
        ("gt_crowbar", (125, 45, 35), "crowbar"), ("gt_soft_mallet", (55, 55, 55), "mallet"),
        ("gt_hard_hammer", (165, 168, 175), "mallet"), ("hammer", (160, 160, 166), "mallet"),
    ]
    for name, color, kind in tool_specs:
        draw_tool_icon(f"resource/items/tools/{name}_icon_32.png", color, kind=kind)
    for head in ("axe", "shovel", "hoe", "knife"):
        material_lump(f"resource/items/tools/stone_{head}_head_icon_32.png", (135, 135, 135), kind="stone")


def generate_component_icons() -> None:
    specs = {
        "components/basic_machine_hull_icon_32.png": ((86, 92, 98), "box"),
        "components/advanced_machine_hull_icon_32.png": ((72, 112, 124), "box"),
        "components/lv_electric_motor_icon_32.png": ((150, 102, 55), "motor"),
        "components/lv_electric_piston_icon_32.png": ((132, 132, 136), "box"),
        "components/lv_robot_arm_icon_32.png": ((130, 108, 80), "box"),
        "components/lv_conveyor_module_icon_32.png": ((80, 85, 86), "conveyor"),
        "components/lv_pump_icon_32.png": ((90, 115, 120), "box"),
        "components/empty_fluid_cell_icon_32.png": ((170, 176, 186), "cell"),
        "components/coal_block_icon_32.png": ((32, 32, 34), "box"),
        "components/coke_brick_icon_32.png": ((62, 58, 55), "box"),
        "components/firebrick_icon_32.png": ((160, 68, 36), "box"),
        "components/stone_plate_icon_32.png": ((126, 126, 130), "box"),
        "components/wood_plate_icon_32.png": ((150, 95, 44), "box"),
        "components/blank_pattern_icon_32.png": ((56, 88, 98), "blueprint"),
        "components/station_blueprint_icon_32.png": ((45, 130, 210), "blueprint"),
        "components/sfm_manager_icon_32.png": ((72, 128, 178), "box"),
        "components/sfm_cable_icon_32.png": ((90, 90, 100), "conveyor"),
        "circuits/vacuum_tube_icon_32.png": ((190, 120, 52), "cell"),
        "circuits/primitive_circuit_icon_32.png": ((115, 78, 42), "circuit"),
        "circuits/basic_circuit_icon_32.png": ((55, 135, 80), "circuit"),
        "circuits/good_circuit_icon_32.png": ((45, 145, 135), "circuit"),
        "circuits/advanced_circuit_icon_32.png": ((45, 95, 145), "circuit"),
    }
    for rel, (color, kind) in specs.items():
        draw_component(f"resource/items/{rel}", color, kind)


def generate_placeables_and_tfc() -> None:
    specs = {
        "placeables/workbench_icon_32.png": ((150, 95, 45), "box"),
        "placeables/stone_furnace_icon_32.png": ((120, 110, 98), "box"),
        "placeables/campfire_icon_32.png": ((218, 88, 28), "cell"),
        "placeables/ladder_icon_32.png": ((150, 84, 42), "conveyor"),
        "placeables/fence_icon_32.png": ((130, 80, 40), "conveyor"),
        "placeables/anvil_icon_32.png": ((82, 82, 88), "box"),
        "tfc/clay_ball_icon_32.png": ((170, 150, 132), "lump"),
        "tfc/straw_icon_32.png": ((210, 190, 88), "straw"),
        "tfc/charcoal_icon_32.png": ((38, 38, 38), "stone"),
        "tfc/flint_and_steel_icon_32.png": ((150, 145, 135), "wrench"),
        "tfc/unfired_bowl_icon_32.png": ((140, 110, 82), "cell"),
        "tfc/unfired_jug_icon_32.png": ((140, 110, 82), "cell"),
        "tfc/unfired_crucible_icon_32.png": ((140, 110, 82), "box"),
        "tfc/unfired_brick_icon_32.png": ((140, 110, 82), "box"),
        "tfc/fired_bowl_icon_32.png": ((190, 105, 65), "cell"),
        "tfc/fired_jug_icon_32.png": ((190, 105, 65), "cell"),
        "tfc/fired_crucible_icon_32.png": ((190, 105, 65), "box"),
        "tfc/refractory_brick_icon_32.png": ((160, 90, 55), "box"),
        "tfc/iron_bloom_icon_32.png": ((120, 72, 36), "stone"),
        "tfc/bellows_icon_32.png": ((150, 98, 55), "box"),
    }
    for rel, (color, kind) in specs.items():
        if kind in {"lump", "stone", "straw"}:
            material_lump(f"resource/items/{rel}", color, kind=kind)
        elif kind == "wrench":
            draw_tool_icon(f"resource/items/{rel}", color, kind="wrench")
        else:
            draw_component(f"resource/items/{rel}", color, kind)


def generate_tree_icons() -> None:
    species = {
        "oak": ((115, 68, 30), (152, 102, 46), (70, 135, 42), None),
        "birch": ((210, 200, 170), (218, 208, 180), (86, 150, 46), None),
        "spruce": ((88, 55, 26), (112, 72, 36), (40, 100, 44), None),
        "acacia": ((140, 86, 34), (170, 106, 46), (80, 140, 36), None),
        "maple": ((108, 62, 28), (145, 82, 35), (75, 145, 45), None),
        "sequoia": ((100, 55, 26), (135, 70, 32), (45, 112, 36), None),
        "cherry": ((128, 72, 58), (165, 92, 72), (190, 92, 130), (220, 42, 68)),
        "olive": ((132, 100, 55), (155, 125, 72), (88, 120, 48), (135, 145, 42)),
    }
    for name, (log, plank, sapling, fruit) in species.items():
        draw_tree_icon(f"resource/items/trees/log_{name}_icon_64.png", log, "log")
        draw_tree_icon(f"resource/items/trees/plank_{name}_icon_64.png", plank, "plank")
        draw_tree_icon(f"resource/items/trees/sapling_{name}_icon_64.png", sapling, "sapling")
        if fruit is not None:
            draw_tree_icon(f"resource/items/trees/fruit_{name}_icon_64.png", fruit, "fruit")


def generate_content_icons() -> None:
    source = {
        "glow_deer_antler": (230, 215, 150), "purifying_pollen": (170, 240, 170),
        "rock_lizard_scale": (150, 135, 105), "crystallized_bone_powder": (220, 214, 202),
        "thunderbird_feather": (130, 150, 230), "magnetic_crystal_shard": (100, 125, 210),
        "sea_serpent_scale": (80, 155, 175), "tidal_gland": (60, 140, 190),
        "blazing_core": (240, 118, 38), "molten_blood_sample": (210, 70, 35),
        "aether_fragment": (180, 155, 230), "blueprint_shard": (110, 170, 200),
        "aberrant_organ": (150, 52, 130), "polluted_source_essence": (95, 36, 85),
    }
    for name, color in source.items():
        material_lump(f"resource/items/source_law/{name}_icon_32.png", color)

    crops = {
        "seed_wheat": (190, 165, 50), "crop_wheat": (220, 190, 64),
        "seed_carrot": (130, 88, 32), "crop_carrot": (220, 112, 42),
        "seed_potato": (110, 88, 42), "crop_potato": (145, 112, 52),
        "seed_cotton": (150, 140, 75), "crop_cotton": (232, 226, 215),
        "seed_herb": (75, 125, 50), "crop_herb": (95, 145, 65),
        "seed_pumpkin": (175, 112, 35), "crop_pumpkin": (225, 130, 36),
        "bone_meal": (235, 235, 226), "flour": (238, 232, 210),
        "bread": (205, 145, 72), "fiber_cotton": (226, 220, 205), "cloth": (190, 180, 165),
    }
    for name, color in crops.items():
        material_lump(f"resource/items/crops/{name}_icon_32.png", color, kind="straw" if "crop" in name else "lump")

    for creature, raw, cooked in [
        ("glow_deer", (200, 120, 120), (155, 92, 65)),
        ("rock_lizard", (165, 140, 95), (125, 95, 58)),
        ("thunderbird", (140, 150, 190), (130, 98, 78)),
        ("sea_serpent", (95, 150, 165), (125, 98, 72)),
        ("blaze_beast", (205, 82, 45), (140, 70, 42)),
    ]:
        material_lump(f"resource/items/food/meat_raw_{creature}_icon_32.png", raw)
        material_lump(f"resource/items/food/meat_cooked_{creature}_icon_32.png", cooked)


def generate_terrain() -> None:
    terrain_specs = {
        "resource/terrain/stone/stone_tile_32.png": "stone",
        "resource/terrain/stone/deepstone_tile_32.png": "deepstone",
        "resource/terrain/stone/granite_tile_32.png": "stone",
        "resource/terrain/stone/basalt_tile_32.png": "deepstone",
        "resource/terrain/stone/marble_tile_32.png": "snow",
        "resource/terrain/stone/sandstone_tile_32.png": "stone",
        "resource/terrain/stone/shale_tile_32.png": "deepstone",
        "resource/terrain/stone/komatiite_tile_32.png": "deepstone",
        "resource/terrain/stone/regolith_tile_32.png": "stone",
        "resource/terrain/stone/anorthosite_tile_32.png": "snow",
        "resource/terrain/dirt/dirt_tile_32.png": "stone",
        "resource/terrain/sand/sand_tile_01_32.png": "snow",
        "resource/terrain/fluid/water_tile_32.png": "water",
        "resource/terrain/fluid/lava_tile_32.png": "lava",
        "resource/terrain/snow/snow_tile_32.png": "snow",
        "resource/terrain/ice/ice_tile_32.png": "ice",
        "resource/terrain/soil/clay_tile_32.png": "stone",
        "resource/terrain/soil/farmland_tile_32.png": "wood",
        "resource/terrain/utility/charcoal_tile_32.png": "charcoal",
        "resource/terrain/utility/bloomery_tile_32.png": "utility",
        "resource/terrain/utility/anvil_tile_32.png": "utility",
        "resource/terrain/utility/pit_kiln_tile_32.png": "utility",
        "resource/terrain/utility/workbench_tile_32.png": "plank",
        "resource/terrain/utility/core_barrier_tile_32.png": "barrier",
        "resource/terrain/plant/straw_tile_32.png": "wood",
        "resource/terrain/plant/leaves_tile_32.png": "leaves",
        "resource/terrain/plant/sapling_tile_32.png": "crop",
        "resource/terrain/wood/log_side_tile_32.png": "wood",
        "resource/terrain/wood/log_top_tile_32.png": "log_top",
        "resource/terrain/wood/ladder_tile_32.png": "plank",
        "resource/terrain/wood/fence_tile_32.png": "plank",
        "resource/terrain/ore/ore_base_32.png": "ore",
        "resource/terrain/crop/crop_seed_32.png": "crop",
        "resource/terrain/crop/crop_sprout_32.png": "crop",
        "resource/terrain/crop/crop_growing_32.png": "crop",
        "resource/terrain/crop/crop_mature_32.png": "crop",
    }
    for rel, kind in terrain_specs.items():
        make_tile(rel, rel, kind)


def apply_imagegen_sources_if_available() -> None:
    source_dir = ROOT / "resource" / "_source" / "imagegen"
    required = [
        source_dir / "item_ingot_template_source.png",
        source_dir / "item_dust_template_source.png",
        source_dir / "item_crushed_template_source.png",
        source_dir / "item_gem_template_source.png",
        source_dir / "terrain_atlas_source.png",
        source_dir / "terrain_atlas2_source.png",
        source_dir / "log_side_source.png",
    ]
    if not all(path.exists() for path in required):
        return
    import postprocess_imagegen_assets

    postprocess_imagegen_assets.main()
    print("[OK] applied imagegen source assets")


def main() -> int:
    generate_material_bases()
    generate_fallback_icons()
    generate_material_icons()
    generate_tool_icons()
    generate_component_icons()
    generate_placeables_and_tfc()
    generate_tree_icons()
    generate_content_icons()
    generate_terrain()
    apply_imagegen_sources_if_available()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
