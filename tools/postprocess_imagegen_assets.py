from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "resource" / "_source" / "imagegen"
PROCESSED = SOURCE / "processed"
ITEM_BASES = ROOT / "resource" / "items" / "material_sets" / "generic"
ITEMS = ROOT / "resource" / "items"


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def crop_alpha_subject(img: Image.Image, padding_ratio: float = 0.08) -> Image.Image:
    alpha = img.getchannel("A")
    mask = alpha.point(lambda value: 255 if value > 10 else 0)
    bbox = mask.getbbox()
    if bbox is None:
        return img
    left, top, right, bottom = bbox
    pad = int(max(right - left, bottom - top) * padding_ratio)
    left = max(0, left - pad)
    top = max(0, top - pad)
    right = min(img.width, right + pad)
    bottom = min(img.height, bottom + pad)
    return img.crop((left, top, right, bottom))


def fit_on_canvas(img: Image.Image, size: int = 32, pad: int = 2) -> Image.Image:
    max_size = size - pad * 2
    scale = min(max_size / img.width, max_size / img.height)
    width = max(1, int(round(img.width * scale)))
    height = max(1, int(round(img.height * scale)))
    resized = img.resize((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(resized, ((size - width) // 2, (size - height) // 2))
    return canvas


def chroma_to_alpha(img: Image.Image, threshold: int = 48, soft: int = 112) -> Image.Image:
    img = img.convert("RGBA")
    key_r, key_g, key_b, _ = img.getpixel((0, 0))
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    src = img.load()
    dst = out.load()
    threshold_sq = threshold * threshold
    soft_sq = soft * soft
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = src[x, y]
            if a <= 0:
                continue
            dist_sq = (r - key_r) ** 2 + (g - key_g) ** 2 + (b - key_b) ** 2
            if dist_sq <= threshold_sq:
                dst[x, y] = (r, g, b, 0)
            elif dist_sq < soft_sq:
                fade = (dist_sq - threshold_sq) / max(1, soft_sq - threshold_sq)
                dst[x, y] = (r, g, b, int(a * fade))
            else:
                dst[x, y] = (r, g, b, a)
    return out


def load_chroma_atlas(source_name: str) -> Image.Image:
    return chroma_to_alpha(Image.open(SOURCE / source_name))


def crop_atlas_cell(atlas: Image.Image, col: int, row: int, columns: int = 4, rows: int = 4) -> Image.Image:
    left = round(atlas.width * col / columns)
    top = round(atlas.height * row / rows)
    right = round(atlas.width * (col + 1) / columns)
    bottom = round(atlas.height * (row + 1) / rows)
    return atlas.crop((left, top, right, bottom))


def save_icon_from_atlas(atlas: Image.Image, col: int, row: int, out_path: str, size: int = 32, pad: int = 2) -> None:
    icon = crop_atlas_cell(atlas, col, row)
    icon = crop_alpha_subject(icon)
    icon = fit_on_canvas(icon, size=size, pad=pad)
    out = ITEMS / out_path
    ensure_parent(out)
    icon.save(out)


def save_icon_map(source_name: str, entries: list[tuple[int, int, str, int, int]]) -> None:
    source_path = SOURCE / source_name
    if not source_path.exists():
        print(f"[WARN] missing atlas source: {source_path.relative_to(ROOT)}")
        return
    atlas = load_chroma_atlas(source_name)
    for col, row, out_path, size, pad in entries:
        save_icon_from_atlas(atlas, col, row, out_path, size=size, pad=pad)


def to_gray_template(img: Image.Image) -> Image.Image:
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    src = img.convert("RGBA").load()
    dst = out.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = src[x, y]
            if a <= 0:
                continue
            lum = int(r * 0.299 + g * 0.587 + b * 0.114)
            lum = max(0, min(255, int((lum - 8) * 1.08)))
            dst[x, y] = (lum, lum, lum, a)
    return out


def save_item_template(source_name: str, out_name: str, pad: int = 2) -> Image.Image:
    img = Image.open(PROCESSED / source_name).convert("RGBA")
    img = crop_alpha_subject(img)
    img = fit_on_canvas(img, pad=pad)
    img = to_gray_template(img)
    out = ITEM_BASES / out_name
    ensure_parent(out)
    img.save(out)
    return img


def save_template_from_atlas(source_name: str, col: int, row: int, out_name: str, pad: int = 2) -> None:
    source_path = SOURCE / source_name
    if not source_path.exists():
        print(f"[WARN] missing atlas source: {source_path.relative_to(ROOT)}")
        return
    atlas = load_chroma_atlas(source_name)
    img = crop_atlas_cell(atlas, col, row)
    img = crop_alpha_subject(img)
    img = fit_on_canvas(img, pad=pad)
    img = to_gray_template(img)
    out = ITEM_BASES / out_name
    ensure_parent(out)
    img.save(out)


def save_material_form_templates() -> None:
    source_name = "item_material_forms_template_source.png"
    templates = [
        (0, 0, "plate_base_32.png", 2),
        (1, 0, "rod_base_32.png", 3),
        (2, 0, "wire_base_32.png", 2),
        (3, 0, "nugget_base_32.png", 5),
        (0, 1, "block_base_32.png", 2),
        (1, 1, "screw_base_32.png", 3),
        (2, 1, "bolt_base_32.png", 3),
        (3, 1, "gear_base_32.png", 2),
        (0, 2, "ring_base_32.png", 3),
        (1, 2, "spring_base_32.png", 3),
        (2, 2, "foil_base_32.png", 2),
        (3, 2, "cell_base_32.png", 2),
        (0, 3, "cable_base_32.png", 3),
        (1, 3, "pipe_base_32.png", 3),
        (2, 3, "dust_pile_base_32.png", 3),
        (3, 3, "dense_plate_base_32.png", 2),
    ]
    for col, row, out_name, pad in templates:
        save_template_from_atlas(source_name, col, row, out_name, pad)


def save_tiny_dust(dust: Image.Image) -> None:
    subject = crop_alpha_subject(dust, padding_ratio=0.0)
    tiny = fit_on_canvas(subject, pad=7)
    tiny.save(ITEM_BASES / "dust_tiny_base_32.png")


def save_ingot_overlay(ingot: Image.Image) -> None:
    overlay = Image.new("RGBA", ingot.size, (0, 0, 0, 0))
    src = ingot.load()
    dst = overlay.load()
    for y in range(ingot.height):
        for x in range(ingot.width):
            r, g, b, a = src[x, y]
            if a <= 0:
                continue
            lum = max(r, g, b)
            if lum < 218:
                continue
            alpha = int(a * min(1.0, (lum - 218) / 37.0) * 0.75)
            dst[x, y] = (255, 255, 255, alpha)
    overlay.save(ITEM_BASES / "ingot_overlay_32.png")


def save_tile(atlas: Image.Image, col: int, row: int, out_path: str) -> None:
    left = round(atlas.width * col / 4)
    top = round(atlas.height * row / 4)
    right = round(atlas.width * (col + 1) / 4)
    bottom = round(atlas.height * (row + 1) / 4)
    tile = atlas.crop((left, top, right, bottom)).resize((32, 32), Image.Resampling.LANCZOS)
    out = ROOT / out_path
    ensure_parent(out)
    tile.save(out)


def save_source_tile(source_name: str, out_path: str) -> None:
    img = Image.open(SOURCE / source_name).convert("RGBA")
    tile = img.resize((32, 32), Image.Resampling.LANCZOS)
    out = ROOT / out_path
    ensure_parent(out)
    tile.save(out)


def save_terrain_tiles() -> None:
    atlas1 = Image.open(SOURCE / "terrain_atlas_source.png").convert("RGBA")
    atlas2 = Image.open(SOURCE / "terrain_atlas2_source.png").convert("RGBA")
    planetary_atlas_path = SOURCE / "terrain_planetary_variants_atlas_source.png"

    first_map = {
        (0, 0): "resource/terrain/stone/stone_tile_32.png",
        (1, 0): "resource/terrain/stone/deepstone_tile_32.png",
        (2, 0): "resource/terrain/stone/granite_tile_32.png",
        (3, 0): "resource/terrain/stone/basalt_tile_32.png",
        (0, 1): "resource/terrain/stone/marble_tile_32.png",
        (1, 1): "resource/terrain/stone/sandstone_tile_32.png",
        (2, 1): "resource/terrain/stone/shale_tile_32.png",
        (3, 1): "resource/terrain/dirt/dirt_tile_32.png",
        (0, 2): "resource/terrain/sand/sand_tile_01_32.png",
        (1, 2): "resource/terrain/soil/clay_tile_32.png",
        (2, 2): "resource/terrain/soil/farmland_tile_32.png",
        (3, 2): "resource/terrain/snow/snow_tile_32.png",
        (0, 3): "resource/terrain/ice/ice_tile_32.png",
        (1, 3): "resource/terrain/utility/charcoal_tile_32.png",
        (2, 3): "resource/terrain/wood/log_top_tile_32.png",
        (3, 3): "resource/terrain/plant/leaves_tile_32.png",
    }
    second_map = {
        (0, 0): "resource/terrain/fluid/water_tile_32.png",
        (1, 0): "resource/terrain/fluid/lava_tile_32.png",
        (2, 0): "resource/terrain/ore/ore_base_32.png",
        (3, 0): "resource/terrain/plant/straw_tile_32.png",
        (0, 1): "resource/terrain/plant/sapling_tile_32.png",
        (1, 1): "resource/terrain/crop/crop_seed_32.png",
        (2, 1): "resource/terrain/crop/crop_sprout_32.png",
        (3, 1): "resource/terrain/crop/crop_growing_32.png",
        (0, 2): "resource/terrain/crop/crop_mature_32.png",
        (1, 2): "resource/terrain/utility/workbench_tile_32.png",
        (3, 2): "resource/terrain/wood/fence_tile_32.png",
        (0, 3): "resource/terrain/utility/anvil_tile_32.png",
        (1, 3): "resource/terrain/utility/core_barrier_tile_32.png",
        (2, 3): "resource/terrain/utility/bloomery_tile_32.png",
        (3, 3): "resource/terrain/utility/pit_kiln_tile_32.png",
    }
    for (col, row), out_path in first_map.items():
        save_tile(atlas1, col, row, out_path)
    for (col, row), out_path in second_map.items():
        save_tile(atlas2, col, row, out_path)
    save_tile(atlas2, 2, 2, "resource/terrain/wood/ladder_tile_32.png")
    save_source_tile("log_side_source.png", "resource/terrain/wood/log_side_tile_32.png")
    if planetary_atlas_path.exists():
        planetary_atlas = Image.open(planetary_atlas_path).convert("RGBA")
        planetary_map = {
            (0, 0): "resource/terrain/stone/anorthosite_tile_32.png",
            (1, 0): "resource/terrain/stone/komatiite_tile_32.png",
            (2, 0): "resource/terrain/stone/regolith_tile_32.png",
            (3, 0): "resource/terrain/stone/scoria_tile_32.png",
            (0, 1): "resource/terrain/sand/sand_tile_01_32.png",
            (1, 1): "resource/terrain/sand/sand_tile_02_32.png",
            (2, 1): "resource/terrain/sand/sand_tile_03_32.png",
            (3, 1): "resource/terrain/sand/sand_tile_04_32.png",
            (0, 2): "resource/terrain/ore/ore_iron_tile_32.png",
            (1, 2): "resource/terrain/ore/ore_copper_tile_32.png",
            (2, 2): "resource/terrain/ore/ore_coal_tile_32.png",
            (3, 2): "resource/terrain/ore/ore_tin_tile_32.png",
            (0, 3): "resource/terrain/stone/meteorite_tile_32.png",
            (1, 3): "resource/terrain/stone/sulfur_crust_tile_32.png",
            (2, 3): "resource/terrain/salt/salt_flat_tile_32.png",
            (3, 3): "resource/terrain/stone/martian_regolith_tile_32.png",
        }
        for (col, row), out_path in planetary_map.items():
            save_tile(planetary_atlas, col, row, out_path)


def save_ore_variant_tiles() -> None:
    atlas_path = SOURCE / "terrain_ore_variants_atlas_source.png"
    if not atlas_path.exists():
        print(f"[WARN] missing atlas source: {atlas_path.relative_to(ROOT)}")
        return
    atlas = Image.open(atlas_path).convert("RGBA")
    ore_map = {
        (0, 0): "resource/terrain/ore/ore_zinc_tile_32.png",
        (1, 0): "resource/terrain/ore/ore_lead_tile_32.png",
        (2, 0): "resource/terrain/ore/ore_silver_tile_32.png",
        (3, 0): "resource/terrain/ore/ore_gold_tile_32.png",
        (0, 1): "resource/terrain/ore/ore_nickel_tile_32.png",
        (1, 1): "resource/terrain/ore/ore_bauxite_tile_32.png",
        (2, 1): "resource/terrain/ore/ore_manganese_tile_32.png",
        (3, 1): "resource/terrain/ore/ore_tungsten_tile_32.png",
        (0, 2): "resource/terrain/ore/ore_titanium_tile_32.png",
        (1, 2): "resource/terrain/ore/ore_platinum_tile_32.png",
        (2, 2): "resource/terrain/ore/ore_cobalt_tile_32.png",
        (3, 2): "resource/terrain/ore/ore_uranium_tile_32.png",
        (0, 3): "resource/terrain/ore/ore_sulfur_tile_32.png",
        (1, 3): "resource/terrain/ore/ore_diamond_tile_32.png",
        (2, 3): "resource/terrain/ore/ore_ruby_tile_32.png",
        (3, 3): "resource/terrain/ore/ore_sapphire_tile_32.png",
    }
    for (col, row), out_path in ore_map.items():
        save_tile(atlas, col, row, out_path)


def save_item_atlases() -> None:
    icon32 = 32
    icon64 = 64
    save_icon_map("item_tools_atlas_source.png", [
        (0, 0, "tools/wooden_pickaxe_icon_32.png", icon32, 2),
        (1, 0, "tools/stone_pickaxe_icon_32.png", icon32, 2),
        (2, 0, "tools/iron_pickaxe_icon_32.png", icon32, 2),
        (3, 0, "tools/wooden_axe_icon_32.png", icon32, 2),
        (0, 1, "tools/stone_axe_icon_32.png", icon32, 2),
        (1, 1, "tools/iron_axe_icon_32.png", icon32, 2),
        (2, 1, "tools/wooden_shovel_icon_32.png", icon32, 2),
        (3, 1, "tools/stone_shovel_icon_32.png", icon32, 2),
        (0, 2, "tools/iron_shovel_icon_32.png", icon32, 2),
        (1, 2, "tools/wooden_sword_icon_32.png", icon32, 2),
        (2, 2, "tools/stone_sword_icon_32.png", icon32, 2),
        (3, 2, "tools/iron_sword_icon_32.png", icon32, 2),
        (0, 3, "tools/stone_hoe_icon_32.png", icon32, 2),
        (1, 3, "tools/stone_knife_icon_32.png", icon32, 2),
        (2, 3, "tools/stone_axe_head_icon_32.png", icon32, 3),
        (3, 3, "tools/stone_shovel_head_icon_32.png", icon32, 3),
    ])
    save_icon_map("item_gt_tools_atlas_source.png", [
        (0, 0, "tools/gt_hammer_icon_32.png", icon32, 2),
        (1, 0, "tools/gt_wrench_icon_32.png", icon32, 2),
        (2, 0, "tools/gt_file_icon_32.png", icon32, 2),
        (3, 0, "tools/gt_screwdriver_icon_32.png", icon32, 2),
        (0, 1, "tools/gt_saw_icon_32.png", icon32, 2),
        (1, 1, "tools/gt_wire_cutter_icon_32.png", icon32, 2),
        (2, 1, "tools/gt_crowbar_icon_32.png", icon32, 2),
        (3, 1, "tools/gt_soft_mallet_icon_32.png", icon32, 2),
        (0, 2, "tools/gt_hard_hammer_icon_32.png", icon32, 2),
        (1, 2, "tools/stone_hoe_head_icon_32.png", icon32, 3),
        (2, 2, "tools/stone_knife_head_icon_32.png", icon32, 4),
        (3, 2, "materials/coal_dust_icon_32.png", icon32, 3),
        (0, 3, "tools/copper_pickaxe_icon_32.png", icon32, 2),
        (1, 3, "tools/copper_axe_icon_32.png", icon32, 2),
        (2, 3, "tools/steel_shovel_icon_32.png", icon32, 2),
        (3, 3, "tools/steel_sword_icon_32.png", icon32, 2),
    ])
    save_icon_map("item_metal_tools_atlas_source.png", [
        (0, 0, "tools/copper_pickaxe_icon_32.png", icon32, 2),
        (1, 0, "tools/copper_axe_icon_32.png", icon32, 2),
        (2, 0, "tools/copper_shovel_icon_32.png", icon32, 2),
        (3, 0, "tools/copper_sword_icon_32.png", icon32, 2),
        (0, 1, "tools/tin_bronze_pickaxe_icon_32.png", icon32, 2),
        (1, 1, "tools/tin_bronze_axe_icon_32.png", icon32, 2),
        (2, 1, "tools/bismuth_bronze_pickaxe_icon_32.png", icon32, 2),
        (3, 1, "tools/bismuth_bronze_axe_icon_32.png", icon32, 2),
        (0, 2, "tools/black_bronze_pickaxe_icon_32.png", icon32, 2),
        (1, 2, "tools/black_bronze_axe_icon_32.png", icon32, 2),
        (2, 2, "tools/steel_pickaxe_icon_32.png", icon32, 2),
        (3, 2, "tools/steel_axe_icon_32.png", icon32, 2),
        (0, 3, "tools/steel_shovel_icon_32.png", icon32, 2),
        (1, 3, "tools/steel_sword_icon_32.png", icon32, 2),
        (2, 3, "tools/bronze_shovel_icon_32.png", icon32, 2),
        (3, 3, "tools/bronze_sword_icon_32.png", icon32, 2),
    ])
    save_icon_map("item_components_atlas_source.png", [
        (0, 0, "components/basic_machine_hull_icon_32.png", icon32, 1),
        (1, 0, "components/advanced_machine_hull_icon_32.png", icon32, 1),
        (2, 0, "components/lv_electric_motor_icon_32.png", icon32, 2),
        (3, 0, "components/lv_electric_piston_icon_32.png", icon32, 2),
        (0, 1, "components/lv_robot_arm_icon_32.png", icon32, 2),
        (1, 1, "components/lv_conveyor_module_icon_32.png", icon32, 2),
        (2, 1, "components/lv_pump_icon_32.png", icon32, 2),
        (3, 1, "components/empty_fluid_cell_icon_32.png", icon32, 2),
        (0, 2, "circuits/vacuum_tube_icon_32.png", icon32, 2),
        (1, 2, "circuits/primitive_circuit_icon_32.png", icon32, 2),
        (2, 2, "circuits/basic_circuit_icon_32.png", icon32, 2),
        (3, 2, "circuits/good_circuit_icon_32.png", icon32, 2),
        (0, 3, "circuits/advanced_circuit_icon_32.png", icon32, 2),
        (1, 3, "components/coal_block_icon_32.png", icon32, 1),
        (2, 3, "components/coke_brick_icon_32.png", icon32, 1),
        (3, 3, "components/firebrick_icon_32.png", icon32, 1),
    ])
    save_icon_map("item_survival_tfc_atlas_source.png", [
        (0, 0, "placeables/workbench_icon_32.png", icon32, 1),
        (1, 0, "placeables/stone_furnace_icon_32.png", icon32, 1),
        (2, 0, "placeables/campfire_icon_32.png", icon32, 2),
        (3, 0, "placeables/ladder_icon_32.png", icon32, 2),
        (0, 1, "placeables/fence_icon_32.png", icon32, 2),
        (1, 1, "placeables/anvil_icon_32.png", icon32, 2),
        (2, 1, "tfc/clay_ball_icon_32.png", icon32, 3),
        (3, 1, "tfc/straw_icon_32.png", icon32, 2),
        (0, 2, "tfc/charcoal_icon_32.png", icon32, 2),
        (1, 2, "tfc/flint_and_steel_icon_32.png", icon32, 3),
        (2, 2, "tfc/unfired_bowl_icon_32.png", icon32, 2),
        (3, 2, "tfc/unfired_jug_icon_32.png", icon32, 2),
        (0, 3, "tfc/unfired_crucible_icon_32.png", icon32, 2),
        (1, 3, "tfc/unfired_brick_icon_32.png", icon32, 3),
        (2, 3, "tfc/fired_bowl_icon_32.png", icon32, 2),
        (3, 3, "tfc/fired_jug_icon_32.png", icon32, 2),
    ])
    save_icon_map("item_extra_components_tfc_atlas_source.png", [
        (0, 0, "tfc/fired_crucible_icon_32.png", icon32, 2),
        (1, 0, "tfc/refractory_brick_icon_32.png", icon32, 3),
        (2, 0, "tfc/iron_bloom_icon_32.png", icon32, 2),
        (3, 0, "tools/hammer_icon_32.png", icon32, 2),
        (0, 1, "tfc/bellows_icon_32.png", icon32, 1),
        (1, 1, "components/station_blueprint_icon_32.png", icon32, 2),
        (2, 1, "components/sfm_manager_icon_32.png", icon32, 1),
        (3, 1, "components/sfm_cable_icon_32.png", icon32, 2),
        (0, 2, "components/stone_plate_icon_32.png", icon32, 2),
        (1, 2, "components/wood_plate_icon_32.png", icon32, 2),
        (2, 2, "components/blank_pattern_icon_32.png", icon32, 2),
        (3, 2, "components/ceramic_brick_mold_icon_32.png", icon32, 2),
        (0, 3, "materials/coal_dust_pile_icon_32.png", icon32, 2),
        (1, 3, "materials/flint_icon_32.png", icon32, 3),
        (2, 3, "materials/chert_icon_32.png", icon32, 3),
        (3, 3, "materials/stick_icon_32.png", icon32, 2),
    ])
    save_icon_map("item_crops_food_atlas_source.png", [
        (0, 0, "crops/seed_wheat_icon_32.png", icon32, 3),
        (1, 0, "crops/crop_wheat_icon_32.png", icon32, 2),
        (2, 0, "crops/seed_carrot_icon_32.png", icon32, 3),
        (3, 0, "crops/crop_carrot_icon_32.png", icon32, 2),
        (0, 1, "crops/seed_potato_icon_32.png", icon32, 2),
        (1, 1, "crops/crop_potato_icon_32.png", icon32, 2),
        (2, 1, "crops/seed_cotton_icon_32.png", icon32, 3),
        (3, 1, "crops/crop_cotton_icon_32.png", icon32, 2),
        (3, 1, "crops/fiber_cotton_icon_32.png", icon32, 4),
        (0, 2, "crops/seed_herb_icon_32.png", icon32, 2),
        (1, 2, "crops/crop_herb_icon_32.png", icon32, 2),
        (2, 2, "crops/seed_pumpkin_icon_32.png", icon32, 3),
        (3, 2, "crops/crop_pumpkin_icon_32.png", icon32, 1),
        (0, 3, "crops/bone_meal_icon_32.png", icon32, 2),
        (1, 3, "crops/flour_icon_32.png", icon32, 1),
        (2, 3, "crops/bread_icon_32.png", icon32, 1),
        (3, 3, "crops/cloth_icon_32.png", icon32, 1),
    ])
    save_icon_map("item_food_meat_atlas_source.png", [
        (0, 0, "food/meat_raw_glow_deer_icon_32.png", icon32, 2),
        (1, 0, "food/meat_cooked_glow_deer_icon_32.png", icon32, 2),
        (2, 0, "food/meat_raw_rock_lizard_icon_32.png", icon32, 2),
        (3, 0, "food/meat_cooked_rock_lizard_icon_32.png", icon32, 2),
        (0, 1, "food/meat_raw_thunderbird_icon_32.png", icon32, 2),
        (1, 1, "food/meat_cooked_thunderbird_icon_32.png", icon32, 2),
        (2, 1, "food/meat_raw_sea_serpent_icon_32.png", icon32, 2),
        (3, 1, "food/meat_cooked_sea_serpent_icon_32.png", icon32, 2),
        (0, 2, "food/meat_raw_blaze_beast_icon_32.png", icon32, 2),
        (1, 2, "food/meat_cooked_blaze_beast_icon_32.png", icon32, 2),
    ])
    save_icon_map("item_source_law_atlas_source.png", [
        (0, 0, "source_law/glow_deer_antler_icon_32.png", icon32, 1),
        (1, 0, "source_law/purifying_pollen_icon_32.png", icon32, 1),
        (2, 0, "source_law/rock_lizard_scale_icon_32.png", icon32, 1),
        (3, 0, "source_law/crystallized_bone_powder_icon_32.png", icon32, 1),
        (0, 1, "source_law/thunderbird_feather_icon_32.png", icon32, 1),
        (1, 1, "source_law/magnetic_crystal_shard_icon_32.png", icon32, 1),
        (2, 1, "source_law/sea_serpent_scale_icon_32.png", icon32, 1),
        (3, 1, "source_law/tidal_gland_icon_32.png", icon32, 1),
        (0, 2, "source_law/blazing_core_icon_32.png", icon32, 1),
        (1, 2, "source_law/molten_blood_sample_icon_32.png", icon32, 1),
        (2, 2, "source_law/aether_fragment_icon_32.png", icon32, 1),
        (3, 2, "source_law/blueprint_shard_icon_32.png", icon32, 1),
        (0, 3, "source_law/aberrant_organ_icon_32.png", icon32, 1),
        (1, 3, "source_law/polluted_source_essence_icon_32.png", icon32, 1),
    ])
    save_icon_map("item_trees_atlas_source.png", [
        (0, 0, "trees/log_oak_icon_64.png", icon64, 4),
        (1, 0, "trees/plank_oak_icon_64.png", icon64, 4),
        (2, 0, "trees/sapling_oak_icon_64.png", icon64, 4),
        (3, 0, "trees/log_birch_icon_64.png", icon64, 4),
        (0, 1, "trees/plank_birch_icon_64.png", icon64, 4),
        (1, 1, "trees/sapling_birch_icon_64.png", icon64, 4),
        (2, 1, "trees/log_spruce_icon_64.png", icon64, 4),
        (3, 1, "trees/plank_spruce_icon_64.png", icon64, 4),
        (0, 2, "trees/sapling_spruce_icon_64.png", icon64, 4),
        (1, 2, "trees/log_acacia_icon_64.png", icon64, 4),
        (2, 2, "trees/plank_acacia_icon_64.png", icon64, 4),
        (3, 2, "trees/sapling_acacia_icon_64.png", icon64, 4),
        (0, 3, "trees/sapling_cherry_icon_64.png", icon64, 4),
        (1, 3, "trees/fruit_cherry_icon_64.png", icon64, 4),
        (2, 3, "trees/sapling_olive_icon_64.png", icon64, 4),
        (3, 3, "trees/fruit_olive_icon_64.png", icon64, 4),
    ])
    save_icon_map("item_extra_trees_atlas_source.png", [
        (0, 0, "trees/log_maple_icon_64.png", icon64, 4),
        (1, 0, "trees/plank_maple_icon_64.png", icon64, 4),
        (2, 0, "trees/sapling_maple_icon_64.png", icon64, 4),
        (3, 0, "trees/log_sequoia_icon_64.png", icon64, 4),
        (0, 1, "trees/plank_sequoia_icon_64.png", icon64, 4),
        (1, 1, "trees/sapling_sequoia_icon_64.png", icon64, 4),
        (2, 1, "trees/log_cherry_icon_64.png", icon64, 4),
        (3, 1, "trees/plank_cherry_icon_64.png", icon64, 4),
        (0, 2, "trees/log_olive_icon_64.png", icon64, 4),
        (1, 2, "trees/plank_olive_icon_64.png", icon64, 4),
        (2, 2, "materials/wood_log_icon_32.png", icon32, 2),
        (3, 2, "materials/wood_plank_icon_32.png", icon32, 2),
    ])


def main() -> int:
    ingot = save_item_template("item_ingot_template_alpha.png", "ingot_base_32.png")
    dust = save_item_template("item_dust_template_alpha.png", "dust_base_32.png")
    save_item_template("item_crushed_template_alpha.png", "crushed_base_32.png")
    save_item_template("item_gem_template_alpha.png", "gem_base_32.png")
    save_tiny_dust(dust)
    save_ingot_overlay(ingot)
    save_material_form_templates()
    save_terrain_tiles()
    save_ore_variant_tiles()
    save_item_atlases()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
