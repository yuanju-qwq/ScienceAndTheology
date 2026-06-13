from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


SIZE = 32
TRANSPARENT = (0, 0, 0, 0)
OUTLINE = (18, 17, 18, 255)
SHADOW = (0, 0, 0, 80)
WHITE = (238, 236, 220, 255)
METAL_DARK = (75, 78, 82, 255)
METAL = (142, 146, 150, 255)
METAL_LIGHT = (220, 220, 210, 255)
GOLD = (202, 143, 58, 255)
COPPER = (190, 95, 38, 255)
IRON = (168, 164, 150, 255)
WOOD_DARK = (86, 52, 25, 255)
WOOD = (156, 96, 42, 255)
WOOD_LIGHT = (219, 150, 73, 255)
STONE_DARK = (74, 75, 78, 255)
STONE = (126, 128, 131, 255)
STONE_LIGHT = (184, 186, 186, 255)
COAL = (27, 28, 31, 255)
COAL_LIGHT = (80, 82, 88, 255)
TEAL = (28, 132, 126, 255)
BLUE = (35, 93, 130, 255)
GREEN = (70, 126, 58, 255)
BROWN_BOARD = (126, 77, 38, 255)


def icon() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    image = Image.new("RGBA", (SIZE, SIZE), TRANSPARENT)
    return image, ImageDraw.Draw(image)


def save(image: Image.Image, root: Path, rel: str) -> None:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


def line(d: ImageDraw.ImageDraw, points: list[tuple[int, int]], fill, width: int = 1) -> None:
    d.line(points, fill=fill, width=width, joint="curve")


def sparkle(d: ImageDraw.ImageDraw, x: int, y: int, color=WHITE) -> None:
    d.point((x, y), fill=color)
    d.point((x - 1, y), fill=color)
    d.point((x + 1, y), fill=color)
    d.point((x, y - 1), fill=color)
    d.point((x, y + 1), fill=color)


def rivet(d: ImageDraw.ImageDraw, x: int, y: int, color=GOLD) -> None:
    d.rectangle((x, y, x + 1, y + 1), fill=OUTLINE)
    d.point((x, y), fill=color)


def iso_board(d: ImageDraw.ImageDraw, fill, accent, advanced: int = 0) -> None:
    shadow = [(6, 17), (17, 9), (29, 16), (18, 25)]
    body = [(4, 15), (16, 7), (29, 15), (17, 24)]
    d.polygon(shadow, fill=SHADOW)
    d.polygon(body, fill=OUTLINE)
    inner = [(7, 15), (16, 9), (26, 15), (17, 22)]
    d.polygon(inner, fill=fill)
    line(d, [(8, 16), (15, 12), (24, 16)], accent, 1)
    line(d, [(10, 18), (16, 14), (23, 18)], (50, 50, 45, 255), 1)
    for p in ((8, 14), (24, 14), (10, 20), (21, 20)):
        rivet(d, *p, color=GOLD)
    d.rounded_rectangle((14, 14, 21, 19), radius=1, fill=OUTLINE)
    d.rectangle((15, 15, 20, 18), fill=(54, 55, 54, 255))
    for x in range(14, 22, 2):
        d.point((x, 20), fill=METAL_LIGHT)
    d.rectangle((9, 13, 11, 16), fill=METAL)
    d.point((9, 13), fill=METAL_LIGHT)
    if advanced >= 1:
        for p in ((24, 18), (25, 17), (26, 16)):
            d.point(p, fill=accent)
        d.rectangle((6, 17, 8, 19), fill=GOLD)
    if advanced >= 2:
        d.rectangle((22, 11, 25, 13), fill=OUTLINE)
        d.rectangle((23, 11, 24, 12), fill=METAL_LIGHT)
        sparkle(d, 25, 18, accent)


def vacuum_tube() -> Image.Image:
    image, d = icon()
    d.ellipse((9, 3, 23, 25), fill=OUTLINE)
    d.ellipse((11, 4, 21, 22), fill=(173, 91, 22, 210))
    d.rectangle((10, 20, 22, 25), fill=OUTLINE)
    d.rectangle((11, 20, 21, 24), fill=METAL_DARK)
    d.rectangle((12, 20, 20, 21), fill=METAL_LIGHT)
    line(d, [(16, 7), (16, 21)], (255, 166, 32, 255), 1)
    d.ellipse((13, 11, 19, 15), outline=(255, 179, 42, 255))
    line(d, [(13, 16), (19, 16)], (255, 142, 21, 255), 1)
    sparkle(d, 12, 8)
    d.rectangle((12, 25, 13, 30), fill=OUTLINE)
    d.rectangle((18, 25, 19, 30), fill=OUTLINE)
    d.rectangle((15, 25, 16, 31), fill=METAL_DARK)
    d.rectangle((21, 24, 22, 29), fill=METAL_DARK)
    return image


def primitive_circuit() -> Image.Image:
    image, d = icon()
    iso_board(d, BROWN_BOARD, WOOD_LIGHT, 0)
    line(d, [(8, 17), (12, 14), (16, 16), (22, 13)], WOOD_LIGHT, 1)
    line(d, [(12, 20), (17, 17), (24, 20)], WOOD_LIGHT, 1)
    d.rectangle((16, 10, 18, 14), fill=(196, 111, 40, 255))
    d.rectangle((16, 9, 18, 10), fill=GOLD)
    return image


def circuit(fill, accent, advanced: int) -> Image.Image:
    image, d = icon()
    iso_board(d, fill, accent, advanced)
    return image


def dust_icon(base, light, dark, copper_marks=False) -> Image.Image:
    image, d = icon()
    piles = ((8, 20, 4), (14, 18, 5), (20, 20, 4), (23, 16, 3), (11, 14, 3))
    for x, y, r in piles:
        d.ellipse((x - r, y - r, x + r, y + r), fill=OUTLINE)
        d.ellipse((x - r + 1, y - r + 1, x + r - 1, y + r - 1), fill=base)
        d.point((x - 1, y - 1), fill=light)
        d.point((x + 1, y + 1), fill=dark)
    if copper_marks:
        d.point((18, 15), fill=GOLD)
        d.point((12, 20), fill=GOLD)
    return image


def crushed_icon(base, light, dark) -> Image.Image:
    image, d = icon()
    rocks = ((8, 18), (14, 14), (20, 19), (23, 14), (12, 23))
    for i, (x, y) in enumerate(rocks):
        pts = [(x - 4, y), (x - 1, y - 4), (x + 4, y - 1), (x + 2, y + 4), (x - 3, y + 3)]
        d.polygon(pts, fill=OUTLINE)
        d.polygon([(px + 1, py) for px, py in pts[:3]] + [(x, y + 2)], fill=base if i % 2 else light)
        d.point((x, y), fill=dark)
    return image


def coal_icon() -> Image.Image:
    image, d = icon()
    pts = [(7, 17), (11, 9), (20, 7), (26, 14), (23, 24), (13, 26)]
    d.polygon(pts, fill=OUTLINE)
    d.polygon([(9, 17), (12, 11), (19, 9), (24, 15), (21, 22), (14, 24)], fill=COAL)
    line(d, [(13, 12), (17, 18), (22, 15)], COAL_LIGHT, 1)
    sparkle(d, 19, 11, (120, 124, 130, 255))
    return image


def wood_log() -> Image.Image:
    image, d = icon()
    d.polygon([(8, 20), (20, 8), (25, 13), (13, 25)], fill=OUTLINE)
    d.polygon([(10, 20), (20, 10), (23, 13), (13, 23)], fill=WOOD)
    line(d, [(12, 19), (20, 11)], WOOD_LIGHT, 1)
    d.ellipse((19, 8, 27, 16), fill=OUTLINE)
    d.ellipse((20, 9, 26, 15), fill=(207, 154, 84, 255))
    d.ellipse((22, 11, 24, 13), outline=WOOD_DARK)
    return image


def wood_plank() -> Image.Image:
    image, d = icon()
    d.polygon([(5, 18), (20, 8), (27, 13), (12, 24)], fill=OUTLINE)
    d.polygon([(7, 18), (20, 10), (25, 13), (12, 22)], fill=WOOD)
    line(d, [(11, 17), (21, 11)], WOOD_LIGHT, 1)
    line(d, [(9, 20), (23, 13)], WOOD_DARK, 1)
    return image


def stick() -> Image.Image:
    image, d = icon()
    line(d, [(8, 24), (23, 8)], OUTLINE, 5)
    line(d, [(9, 23), (22, 9)], WOOD, 3)
    line(d, [(13, 19), (18, 14)], WOOD_LIGHT, 1)
    return image


def plate(fill, light) -> Image.Image:
    image, d = icon()
    d.polygon([(6, 17), (17, 10), (27, 15), (16, 23)], fill=OUTLINE)
    d.polygon([(8, 17), (17, 12), (25, 15), (16, 21)], fill=fill)
    line(d, [(11, 17), (17, 14), (22, 16)], light, 1)
    return image


def block_icon(fill, light, dark) -> Image.Image:
    image, d = icon()
    d.rectangle((8, 9, 24, 25), fill=OUTLINE)
    d.rectangle((10, 11, 22, 23), fill=fill)
    line(d, [(10, 11), (22, 11), (22, 23)], light, 1)
    line(d, [(10, 23), (22, 23)], dark, 1)
    return image


def fluid_cell() -> Image.Image:
    image, d = icon()
    d.rounded_rectangle((11, 4, 21, 27), radius=3, fill=OUTLINE)
    d.rounded_rectangle((13, 6, 19, 25), radius=2, fill=(178, 190, 196, 255))
    d.rectangle((13, 5, 19, 8), fill=METAL_LIGHT)
    d.rectangle((14, 13, 18, 24), fill=(94, 126, 142, 110))
    sparkle(d, 14, 9)
    return image


def machine_hull(fill, light) -> Image.Image:
    image, d = icon()
    d.rectangle((6, 8, 26, 25), fill=OUTLINE)
    d.rectangle((8, 10, 24, 23), fill=fill)
    d.rectangle((11, 13, 21, 20), fill=(42, 45, 48, 255))
    line(d, [(8, 10), (24, 10)], light, 1)
    for p in ((9, 11), (22, 11), (9, 21), (22, 21)):
        rivet(d, *p, METAL_LIGHT)
    return image


def motor() -> Image.Image:
    image, d = icon()
    d.ellipse((7, 11, 22, 25), fill=OUTLINE)
    d.ellipse((9, 13, 20, 23), fill=METAL)
    d.rectangle((18, 14, 27, 21), fill=OUTLINE)
    d.rectangle((18, 16, 25, 19), fill=COPPER)
    line(d, [(11, 15), (18, 15), (12, 18), (18, 18), (12, 21), (18, 21)], GOLD, 1)
    return image


def piston() -> Image.Image:
    image, d = icon()
    d.rectangle((6, 15, 18, 24), fill=OUTLINE)
    d.rectangle((8, 17, 16, 22), fill=METAL)
    d.rectangle((15, 10, 25, 16), fill=OUTLINE)
    d.rectangle((16, 11, 24, 15), fill=METAL_LIGHT)
    line(d, [(16, 19), (25, 11)], METAL_DARK, 3)
    line(d, [(17, 18), (24, 12)], METAL_LIGHT, 1)
    return image


def robot_arm() -> Image.Image:
    image, d = icon()
    d.rectangle((7, 22, 17, 26), fill=OUTLINE)
    d.rectangle((9, 23, 15, 25), fill=METAL)
    line(d, [(13, 22), (18, 15), (23, 10)], OUTLINE, 5)
    line(d, [(13, 22), (18, 15), (23, 10)], GOLD, 3)
    d.arc((20, 6, 30, 16), 60, 240, fill=OUTLINE, width=3)
    d.point((26, 9), fill=METAL_LIGHT)
    return image


def conveyor() -> Image.Image:
    image, d = icon()
    d.polygon([(5, 18), (16, 11), (27, 17), (16, 25)], fill=OUTLINE)
    d.polygon([(7, 18), (16, 13), (25, 17), (16, 23)], fill=(43, 47, 48, 255))
    for x in (10, 15, 20):
        line(d, [(x, 17), (x + 3, 19)], METAL_LIGHT, 1)
    return image


def pump() -> Image.Image:
    image, d = icon()
    d.ellipse((7, 10, 21, 24), fill=OUTLINE)
    d.ellipse((9, 12, 19, 22), fill=(72, 116, 130, 255))
    d.rectangle((18, 15, 27, 19), fill=OUTLINE)
    d.rectangle((20, 16, 25, 18), fill=METAL)
    d.arc((11, 13, 18, 20), 20, 330, fill=METAL_LIGHT, width=2)
    return image


def blank_pattern() -> Image.Image:
    image, d = icon()
    d.polygon([(7, 17), (16, 9), (25, 16), (16, 24)], fill=OUTLINE)
    d.polygon([(9, 17), (16, 11), (23, 16), (16, 22)], fill=(45, 73, 81, 255))
    d.rectangle((13, 14, 19, 19), fill=(108, 154, 162, 255))
    line(d, [(11, 17), (16, 13), (21, 17)], METAL_LIGHT, 1)
    return image


def tool_icon(kind: str, head, handle=WOOD) -> Image.Image:
    image, d = icon()
    line(d, [(9, 25), (22, 10)], OUTLINE, 5)
    line(d, [(10, 24), (21, 11)], handle, 3)
    if kind == "pickaxe":
        line(d, [(9, 11), (17, 7), (26, 10)], OUTLINE, 5)
        line(d, [(10, 11), (17, 9), (25, 10)], head, 3)
    elif kind == "axe":
        d.polygon([(15, 7), (25, 9), (23, 18), (15, 16)], fill=OUTLINE)
        d.polygon([(17, 9), (23, 10), (21, 16), (17, 15)], fill=head)
    elif kind == "shovel":
        d.polygon([(18, 6), (25, 10), (22, 18), (15, 15)], fill=OUTLINE)
        d.polygon([(19, 8), (23, 11), (21, 16), (17, 15)], fill=head)
    elif kind == "sword":
        line(d, [(12, 22), (24, 6)], OUTLINE, 5)
        line(d, [(13, 21), (23, 7)], head, 3)
        line(d, [(10, 20), (16, 25)], OUTLINE, 3)
        line(d, [(11, 20), (15, 24)], GOLD, 1)
    return image


def gt_tool(name: str) -> Image.Image:
    image, d = icon()
    if name in ("hammer", "hard_hammer"):
        line(d, [(10, 25), (20, 14)], OUTLINE, 5)
        line(d, [(11, 24), (19, 15)], WOOD, 3)
        d.rectangle((12, 7, 26, 14), fill=OUTLINE)
        d.rectangle((14, 8, 24, 13), fill=METAL if name == "hammer" else METAL_DARK)
        line(d, [(15, 8), (23, 8)], METAL_LIGHT, 1)
    elif name == "wrench":
        line(d, [(8, 25), (21, 12)], OUTLINE, 5)
        line(d, [(9, 24), (20, 13)], METAL, 3)
        d.arc((17, 5, 29, 17), 40, 300, fill=OUTLINE, width=4)
        d.arc((19, 7, 27, 15), 45, 300, fill=METAL_LIGHT, width=2)
    elif name == "file":
        line(d, [(8, 25), (23, 10)], OUTLINE, 4)
        line(d, [(10, 23), (23, 10)], METAL, 2)
        for i in range(4):
            d.point((15 + i * 2, 16 - i * 2), fill=METAL_DARK)
    elif name == "screwdriver":
        line(d, [(8, 25), (22, 11)], OUTLINE, 4)
        line(d, [(9, 24), (18, 15)], (218, 82, 24, 255), 2)
        line(d, [(18, 15), (25, 8)], METAL_LIGHT, 2)
    elif name == "saw":
        d.polygon([(7, 22), (25, 10), (27, 15), (10, 26)], fill=OUTLINE)
        d.polygon([(9, 22), (24, 12), (25, 14), (11, 24)], fill=METAL_LIGHT)
        for i in range(5):
            d.point((12 + i * 3, 22 - i * 2), fill=OUTLINE)
    elif name == "wire_cutter":
        line(d, [(10, 25), (16, 16)], OUTLINE, 4)
        line(d, [(22, 25), (16, 16)], OUTLINE, 4)
        line(d, [(11, 24), (16, 16)], (45, 75, 115, 255), 2)
        line(d, [(21, 24), (16, 16)], (45, 75, 115, 255), 2)
        d.arc((12, 7, 22, 17), 210, 330, fill=METAL_LIGHT, width=3)
    elif name == "crowbar":
        line(d, [(8, 25), (23, 9)], OUTLINE, 5)
        line(d, [(9, 24), (22, 10)], (120, 40, 34, 255), 3)
        d.arc((18, 5, 28, 15), 80, 230, fill=OUTLINE, width=3)
    elif name == "soft_mallet":
        line(d, [(10, 25), (20, 14)], OUTLINE, 5)
        line(d, [(11, 24), (19, 15)], WOOD, 3)
        d.rounded_rectangle((12, 7, 26, 15), radius=3, fill=OUTLINE)
        d.rounded_rectangle((14, 8, 24, 14), radius=2, fill=(35, 35, 35, 255))
    return image


def placeable(name: str) -> Image.Image:
    image, d = icon()
    if name == "workbench":
        d.rectangle((7, 10, 25, 24), fill=OUTLINE)
        d.rectangle((9, 12, 23, 22), fill=WOOD)
        line(d, [(9, 15), (23, 15)], WOOD_DARK, 1)
        line(d, [(12, 12), (12, 22)], WOOD_LIGHT, 1)
    elif name == "furnace":
        d.rectangle((7, 8, 25, 25), fill=OUTLINE)
        d.rectangle((9, 10, 23, 23), fill=STONE)
        d.rectangle((12, 14, 20, 21), fill=OUTLINE)
        d.rectangle((14, 17, 18, 21), fill=(226, 89, 30, 255))
        for p in ((10, 11), (18, 11), (22, 19)):
            d.point(p, fill=STONE_LIGHT)
    elif name == "ladder":
        line(d, [(10, 6), (10, 27)], OUTLINE, 3)
        line(d, [(22, 6), (22, 27)], OUTLINE, 3)
        line(d, [(10, 10), (22, 10)], WOOD_LIGHT, 3)
        line(d, [(10, 16), (22, 16)], WOOD_LIGHT, 3)
        line(d, [(10, 22), (22, 22)], WOOD_LIGHT, 3)
        line(d, [(10, 6), (10, 27)], WOOD, 1)
        line(d, [(22, 6), (22, 27)], WOOD, 1)
    return image


def write_all(root: Path) -> None:
    assets: dict[str, Image.Image] = {
        "circuits/vacuum_tube_icon_32.png": vacuum_tube(),
        "circuits/primitive_circuit_icon_32.png": primitive_circuit(),
        "circuits/basic_circuit_icon_32.png": circuit(GREEN, (155, 214, 87, 255), 0),
        "circuits/good_circuit_icon_32.png": circuit(TEAL, (53, 230, 210, 255), 1),
        "circuits/advanced_circuit_icon_32.png": circuit(BLUE, (52, 225, 240, 255), 2),
        "materials/wood_log_icon_32.png": wood_log(),
        "materials/wood_plank_icon_32.png": wood_plank(),
        "materials/stick_icon_32.png": stick(),
        "materials/stone_dust_icon_32.png": dust_icon(STONE, STONE_LIGHT, STONE_DARK),
        "materials/stone_tiny_dust_icon_32.png": dust_icon(STONE, STONE_LIGHT, STONE_DARK),
        "materials/coal_icon_32.png": coal_icon(),
        "materials/crushed_copper_icon_32.png": crushed_icon(COPPER, (226, 135, 69, 255), (105, 55, 31, 255)),
        "materials/crushed_iron_icon_32.png": crushed_icon(IRON, METAL_LIGHT, STONE_DARK),
        "materials/copper_dust_icon_32.png": dust_icon(COPPER, (232, 145, 70, 255), (104, 50, 25, 255), True),
        "materials/iron_dust_icon_32.png": dust_icon(IRON, METAL_LIGHT, STONE_DARK),
        "components/basic_machine_hull_icon_32.png": machine_hull((70, 76, 78, 255), METAL_LIGHT),
        "components/advanced_machine_hull_icon_32.png": machine_hull((48, 96, 105, 255), (116, 210, 218, 255)),
        "components/lv_electric_motor_icon_32.png": motor(),
        "components/lv_electric_piston_icon_32.png": piston(),
        "components/lv_robot_arm_icon_32.png": robot_arm(),
        "components/lv_conveyor_module_icon_32.png": conveyor(),
        "components/lv_pump_icon_32.png": pump(),
        "components/empty_fluid_cell_icon_32.png": fluid_cell(),
        "components/coal_block_icon_32.png": block_icon(COAL, COAL_LIGHT, (5, 5, 7, 255)),
        "components/coke_brick_icon_32.png": block_icon((58, 51, 44, 255), (100, 83, 68, 255), (31, 27, 25, 255)),
        "components/firebrick_icon_32.png": block_icon((163, 67, 34, 255), (222, 114, 57, 255), (101, 39, 23, 255)),
        "components/stone_plate_icon_32.png": plate(STONE, STONE_LIGHT),
        "components/wood_plate_icon_32.png": plate(WOOD, WOOD_LIGHT),
        "components/blank_pattern_icon_32.png": blank_pattern(),
        "placeables/workbench_icon_32.png": placeable("workbench"),
        "placeables/stone_furnace_icon_32.png": placeable("furnace"),
        "placeables/ladder_icon_32.png": placeable("ladder"),
    }

    for material_name, head, handle in (
        ("wooden", WOOD, WOOD_DARK),
        ("stone", STONE, WOOD),
        ("iron", METAL_LIGHT, WOOD),
    ):
        for kind in ("pickaxe", "axe", "shovel", "sword"):
            assets[f"tools/{material_name}_{kind}_icon_32.png"] = tool_icon(kind, head, handle)

    for name in (
        "hammer",
        "wrench",
        "file",
        "screwdriver",
        "saw",
        "wire_cutter",
        "crowbar",
        "soft_mallet",
        "hard_hammer",
    ):
        assets[f"tools/gt_{name}_icon_32.png"] = gt_tool(name)

    for rel, image in sorted(assets.items()):
        if image.size != (SIZE, SIZE):
            raise ValueError(f"{rel} generated at {image.size}, expected 32x32")
        save(image, root, rel)

    print(f"[assets] Generated {len(assets)} native 32x32 item icon sources in '{root}'.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate native 32x32 source item icons.")
    parser.add_argument("project_root", type=Path, nargs="?", default=Path("."))
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    source_root = args.project_root.resolve() / "resource" / "source" / "items_icons"
    write_all(source_root)


if __name__ == "__main__":
    main()
