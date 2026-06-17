class_name BuiltinTerrainContent
extends RefCounted

const MAT_AIR := 0
const MAT_STONE := 1
const MAT_DIRT := 2
const MAT_SAND := 3
const MAT_WATER := 4
const MAT_LAVA := 5
const MAT_ORE_IRON := 6
const MAT_ORE_COPPER := 7
const MAT_ORE_COAL := 8
const MAT_WOOD := 9
const MAT_LEAVES := 10
const MAT_LADDER := 11
const MAT_WORKBENCH := 12
const MAT_DEEPSTONE := 13
const MAT_CORE_BARRIER := 14

const FLAG_WALKABLE := 1
const FLAG_SOLID := 2
const FLAG_LIQUID := 4
const FLAG_MINEABLE := 8
const FLAG_CLIMBABLE := 16
const FLAG_INDESTRUCTIBLE := 32
const FLAG_GRAVITY_FALL := 64
const FLAG_COLLAPSE_RISK := 128
const FLAG_SUPPORT_BEAM := 256


static func create_default_config() -> Resource:
	var registry: Object = ClassDB.instantiate("GDTerrainContentRegistry")
	if registry == null:
		push_error("BuiltinTerrainContent: GDTerrainContentRegistry is not registered.")
		return null
	_register_builtin_material_interactions(registry)
	_register_builtin_material_visuals(registry)
	_register_builtin_generation_rules(registry)
	_register_runtime_material_ids(registry)
	return registry.freeze()


static func _register_builtin_material_interactions(registry: Object) -> void:
	registry.register_material({
		"id": MAT_AIR,
		"key": "snt:air",
		"display_name": "Air",
		"flags": 0,
		"hardness": 0.0,
	})
	registry.register_material({
		"id": MAT_STONE,
		"key": "snt:stone",
		"display_name": "Stone",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_COLLAPSE_RISK,
		"hardness": 1.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"collapse_chance": 0.3,
		"rock_layer_key": "snt:granite",
		"drops": [{ "item_key": "dust.stone", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_DIRT,
		"key": "snt:dirt",
		"display_name": "Dirt",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.5,
		"required_tool_tag": "shovel",
		"required_mining_level": 0,
		"drops": [{ "item_key": "tiny_dust.stone", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_SAND,
		"key": "snt:sand",
		"display_name": "Sand",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE | FLAG_GRAVITY_FALL,
		"hardness": 0.45,
		"required_tool_tag": "shovel",
		"required_mining_level": 0,
		"drops": [{ "item_key": "tiny_dust.stone", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_WATER,
		"key": "snt:water",
		"display_name": "Water",
		"flags": FLAG_LIQUID,
		"hardness": 100.0,
	})
	registry.register_material({
		"id": MAT_LAVA,
		"key": "snt:lava",
		"display_name": "Lava",
		"flags": FLAG_LIQUID,
		"hardness": 100.0,
	})
	registry.register_material({
		"id": MAT_ORE_IRON,
		"key": "snt:ore_iron",
		"display_name": "Iron Ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.iron", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_ORE_COPPER,
		"key": "snt:ore_copper",
		"display_name": "Copper Ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.copper", "count": 1, "min_count": 1, "max_count": 2 }],
	})
	registry.register_material({
		"id": MAT_ORE_COAL,
		"key": "snt:ore_coal",
		"display_name": "Coal Ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.8,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "gem.coal", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_WOOD,
		"key": "snt:wood",
		"display_name": "Wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "dust.wood", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_LEAVES,
		"key": "snt:leaves",
		"display_name": "Leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_LADDER,
		"key": "snt:ladder",
		"display_name": "Ladder",
		"flags": FLAG_MINEABLE | FLAG_CLIMBABLE,
		"hardness": 0.5,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "ladder", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_WORKBENCH,
		"key": "snt:workbench",
		"display_name": "Workbench",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "workbench", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_DEEPSTONE,
		"key": "snt:deepstone",
		"display_name": "Deepstone",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_COLLAPSE_RISK,
		"hardness": 10.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 3,
		"collapse_chance": 0.5,
		"rock_layer_key": "snt:deeprock",
		"drops": [{ "item_key": "dust.stone", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_CORE_BARRIER,
		"key": "snt:core_barrier",
		"display_name": "Core Barrier",
		"flags": FLAG_SOLID | FLAG_INDESTRUCTIBLE,
		"hardness": -1.0,
	})


static func _register_builtin_material_visuals(registry: Object) -> void:
	var visuals := [
		{ "material_key": "snt:air", "dimension": "overworld", "enabled": false,
		  "albedo_color": Color(0, 0, 0, 0) },
		{ "material_key": "snt:stone", "dimension": "overworld",
		  "albedo_color": Color(0.46, 0.47, 0.45) },
		{ "material_key": "snt:dirt", "dimension": "overworld",
		  "albedo_color": Color(0.33, 0.25, 0.14),
		  "top_texture": "res://resource/terrain/dirt/dirt_top_32.png",
		  "bottom_texture": "res://resource/terrain/dirt/dirt_bottom_32.png",
		  "sides_texture": "res://resource/terrain/dirt/dirt_side_32.png" },
		{ "material_key": "snt:sand", "dimension": "overworld",
		  "albedo_color": Color(0.73, 0.64, 0.40),
		  "sides_texture": "res://resource/terrain/sand/sand_tile_01_32.png",
		  "sides_variant_count": 4 },
		{ "material_key": "snt:water", "dimension": "overworld",
		  "albedo_color": Color(0.18, 0.39, 0.74, 0.78),
		  "transparent": true, "cull_disabled": true, "roughness": 0.1 },
		{ "material_key": "snt:lava", "dimension": "overworld",
		  "albedo_color": Color(0.95, 0.28, 0.08),
		  "emissive_color": Color(0.8, 0.2, 0.05), "roughness": 0.3 },
		{ "material_key": "snt:ore_iron", "dimension": "overworld",
		  "albedo_color": Color(0.65, 0.58, 0.50),
		  "overlays": [{ "texture_path": "res://resource/terrain/ore/ore_iron_overlay_32.png", "blend": 0.6 }] },
		{ "material_key": "snt:ore_copper", "dimension": "overworld",
		  "albedo_color": Color(0.72, 0.37, 0.18),
		  "overlays": [{ "texture_path": "res://resource/terrain/ore/ore_copper_overlay_32.png", "blend": 0.6 }] },
		{ "material_key": "snt:ore_coal", "dimension": "overworld",
		  "albedo_color": Color(0.13, 0.13, 0.13),
		  "overlays": [{ "texture_path": "res://resource/terrain/ore/ore_coal_overlay_32.png", "blend": 0.6 }] },
		{ "material_key": "snt:wood", "dimension": "overworld",
		  "albedo_color": Color(0.45, 0.27, 0.12) },
		{ "material_key": "snt:leaves", "dimension": "overworld",
		  "albedo_color": Color(0.21, 0.42, 0.20), "cull_disabled": true },
		{ "material_key": "snt:ladder", "dimension": "overworld",
		  "albedo_color": Color(0.55, 0.30, 0.15), "cull_disabled": true },
		{ "material_key": "snt:workbench", "dimension": "overworld",
		  "albedo_color": Color(0.60, 0.40, 0.20) },
		{ "material_key": "snt:deepstone", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.30, 0.32) },
		{ "material_key": "snt:core_barrier", "dimension": "overworld",
		  "albedo_color": Color(0.10, 0.0, 0.15),
		  "emissive_color": Color(0.15, 0.0, 0.25), "roughness": 0.5 },
	]
	for visual in visuals:
		registry.register_material_visual(visual)


static func _register_builtin_generation_rules(registry: Object) -> void:
	registry.set_material_roles({
		"air_key": "snt:air",
		"stone_key": "snt:stone",
		"dirt_key": "snt:dirt",
		"sand_key": "snt:sand",
		"water_key": "snt:water",
		"lava_key": "snt:lava",
		"ore_iron_key": "snt:ore_iron",
		"ore_copper_key": "snt:ore_copper",
		"ore_coal_key": "snt:ore_coal",
		"wood_key": "snt:wood",
		"leaves_key": "snt:leaves",
		"deepstone_key": "snt:deepstone",
		"core_barrier_key": "snt:core_barrier",
	})

	registry.register_base_terrain_rule({
		"dimension": "overworld",
		"mode": "surface_elevation",
		"default_material_key": "snt:dirt",
		"low_elevation_material_key": "snt:water",
		"high_elevation_material_key": "snt:stone",
		"elevation_scale": 0.02,
		"elevation_octaves": 4,
		"detail_scale": 0.05,
		"detail_octaves": 3,
		"water_elevation_max": -0.25,
		"water_detail_max": 0.3,
		"stone_elevation_abs_min": 0.55,
	})
	registry.register_biome_rule({
		"key": "snt:desert_sand",
		"dimension": "overworld",
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:sand",
		"temperature_min": 0.3,
		"humidity_max": -0.2,
	})
	registry.register_biome_rule({
		"key": "snt:beach_sand",
		"dimension": "overworld",
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:sand",
		"requires_near_material": true,
		"near_material_key": "snt:water",
		"near_radius": 2,
	})
	registry.register_biome_rule({
		"key": "snt:rocky_highlands",
		"dimension": "overworld",
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:stone",
		"temperature_max": -0.4,
		"humidity_max": -0.1,
	})
	registry.register_ore_vein_rule({
		"key": "snt:ore_iron",
		"dimension": "overworld",
		"host_material_key": "snt:stone",
		"ore_material_key": "snt:ore_iron",
		"combined_min": 0.5,
		"combined_max": 1.0,
	})
	registry.register_ore_vein_rule({
		"key": "snt:ore_copper",
		"dimension": "overworld",
		"host_material_key": "snt:stone",
		"ore_material_key": "snt:ore_copper",
		"combined_min": 0.25,
		"combined_max": 0.5,
	})
	registry.register_ore_vein_rule({
		"key": "snt:ore_coal",
		"dimension": "overworld",
		"host_material_key": "snt:stone",
		"ore_material_key": "snt:ore_coal",
		"combined_min": 0.05,
		"combined_max": 0.25,
	})

	# Rock layer rules: regional rock types that determine underground composition.
	# Different areas of the planet have different base rock, affecting
	# hardness, collapse chance, and which ores can spawn.
	registry.register_rock_layer_rule({
		"key": "snt:granite",
		"dimension": "overworld",
		"rock_material_key": "snt:stone",
		"noise_scale": 0.005,
		"noise_octaves": 3,
		"noise_min": -1.0,
		"noise_max": 0.0,
		"depth_min": 0.0,
		"depth_max": 100.0,
		"hardness_multiplier": 1.0,
		"collapse_chance": 0.3,
		"associated_ores": ["snt:ore_iron", "snt:ore_copper"],
	})
	registry.register_rock_layer_rule({
		"key": "snt:basalt",
		"dimension": "overworld",
		"rock_material_key": "snt:stone",
		"noise_scale": 0.005,
		"noise_octaves": 3,
		"noise_min": 0.0,
		"noise_max": 1.0,
		"depth_min": 0.0,
		"depth_max": 100.0,
		"hardness_multiplier": 1.2,
		"collapse_chance": 0.25,
		"associated_ores": ["snt:ore_iron"],
	})
	registry.register_rock_layer_rule({
		"key": "snt:deeprock",
		"dimension": "overworld",
		"rock_material_key": "snt:deepstone",
		"noise_scale": 0.003,
		"noise_octaves": 2,
		"noise_min": -1.0,
		"noise_max": 1.0,
		"depth_min": 60.0,
		"depth_max": 10000.0,
		"hardness_multiplier": 1.5,
		"collapse_chance": 0.5,
		"associated_ores": [],
	})

	# Planet configuration: spherical world with radius 512 blocks.
	# Center is placed so that the player starts near the "north pole" surface.
	# Core is 5% of radius (25 blocks), mantle boundary at 50% (256 blocks).
	# Players can tunnel through the crust and mantle to reach the other side,
	# but the inner core barrier (25 blocks radius) is indestructible.
	registry.register_planet_config({
		"dimension": "overworld",
		"planet_radius": 512.0,
		"center_x": 0.0,
		"center_y": -512.0,
		"center_z": 0.0,
		"terrain_height_scale": 16.0,
		"elevation_noise_scale": 0.008,
		"elevation_octaves": 5,
		"detail_noise_scale": 0.03,
		"detail_octaves": 3,
		"cave_noise_scale": 0.04,
		"cave_octaves": 4,
		"cave_threshold": 0.35,
		"sea_level_fraction": 0.3,
		"core_radius_ratio": 0.05,
		"mantle_radius_ratio": 0.5,
		"core_boundary_noise_scale": 0.02,
		"core_boundary_noise_octaves": 3,
		"core_boundary_noise_amplitude": 0.15,
	})


# Register runtime material IDs for blocks placed by players (not by terrain generation).
# These are separate from TerrainMaterialRoles because they are never consumed
# by any terrain pass. The command server uses these IDs to write terrain cells.
static func _register_runtime_material_ids(registry: Object) -> void:
	registry.set_runtime_material_ids({
		"ladder": "snt:ladder",
		"workbench": "snt:workbench",
	})
