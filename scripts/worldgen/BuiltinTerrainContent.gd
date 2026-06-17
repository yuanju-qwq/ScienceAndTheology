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

// Tree species materials: wood, leaves, sapling per species.
const MAT_OAK_WOOD := 15
const MAT_OAK_LEAVES := 16
const MAT_OAK_SAPLING := 17
const MAT_BIRCH_WOOD := 18
const MAT_BIRCH_LEAVES := 19
const MAT_BIRCH_SAPLING := 20
const MAT_SPRUCE_WOOD := 21
const MAT_SPRUCE_LEAVES := 22
const MAT_SPRUCE_SAPLING := 23
const MAT_ACACIA_WOOD := 24
const MAT_ACACIA_LEAVES := 25
const MAT_ACACIA_SAPLING := 26
const MAT_MAPLE_WOOD := 27
const MAT_MAPLE_LEAVES := 28
const MAT_MAPLE_SAPLING := 29
const MAT_SEQUOIA_WOOD := 30
const MAT_SEQUOIA_LEAVES := 31
const MAT_SEQUOIA_SAPLING := 32
const MAT_CHERRY_WOOD := 33
const MAT_CHERRY_LEAVES := 34
const MAT_CHERRY_SAPLING := 35
const MAT_OLIVE_WOOD := 36
const MAT_OLIVE_LEAVES := 37
const MAT_OLIVE_SAPLING := 38

// Canopy shape enum (must match C++ CanopyShape).
const CANOPY_SPHERE := 0
const CANOPY_CONE := 1
const CANOPY_UMBRELLA := 2
const CANOPY_COLUMN := 3

const FLAG_WALKABLE := 1
const FLAG_SOLID := 2
const FLAG_LIQUID := 4
const FLAG_MINEABLE := 8
const FLAG_CLIMBABLE := 16
const FLAG_INDESTRUCTIBLE := 32
const FLAG_GRAVITY_FALL := 64
const FLAG_COLLAPSE_RISK := 128
const FLAG_SUPPORT_BEAM := 256


# Create the default single-planet config (backward compatible).
# Uses the original "overworld" planet with radius 512.
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


# Create a multi-planet config from an array of PlanetDescriptor resources.
# Registers base terrain rules, biome rules, ore vein rules, rock layer rules,
# and planet configs for each landable planet. Stars are skipped.
# All planets share the same material set and biome/ore/rock rules,
# but each planet has its own dimension_id and PlanetConfig.
static func create_config_for_universe(universe_planets: Array[PlanetDescriptor]) -> Resource:
	var registry: Object = ClassDB.instantiate("GDTerrainContentRegistry")
	if registry == null:
		push_error("BuiltinTerrainContent: GDTerrainContentRegistry is not registered.")
		return null
	_register_builtin_material_interactions(registry)
	_register_builtin_material_visuals(registry)
	_register_runtime_material_ids(registry)

	# Register generation rules and planet configs for each landable planet.
	for planet in universe_planets:
		if planet.is_star:
			continue
		_register_planet_generation_rules(registry, planet)
		registry.register_planet_config(planet.to_planet_config_dict())

	return registry.freeze()


# Register generation rules for a single planet dimension.
# Each planet gets its own base terrain rule, biome rules, ore vein rules,
# and rock layer rules, all scoped to the planet's dimension_id.
static func _register_planet_generation_rules(registry: Object, planet: PlanetDescriptor) -> void:
	var dim := String(planet.dimension_id)

	# Base terrain rule — same structure as overworld, but per-dimension.
	registry.register_base_terrain_rule({
		"dimension": dim,
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

	# Biome rules — same as overworld, scoped to this dimension.
	registry.register_biome_rule({
		"key": "snt:desert_sand",
		"dimension": dim,
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:sand",
		"temperature_min": 0.3,
		"humidity_max": -0.2,
	})
	registry.register_biome_rule({
		"key": "snt:beach_sand",
		"dimension": dim,
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:sand",
		"requires_near_material": true,
		"near_material_key": "snt:water",
		"near_radius": 2,
	})
	registry.register_biome_rule({
		"key": "snt:rocky_highlands",
		"dimension": dim,
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:stone",
		"temperature_max": -0.4,
		"humidity_max": -0.1,
	})

	# Ore vein rules — same as overworld, scoped to this dimension.
	registry.register_ore_vein_rule({
		"key": "snt:ore_iron",
		"dimension": dim,
		"host_material_key": "snt:stone",
		"ore_material_key": "snt:ore_iron",
		"combined_min": 0.5,
		"combined_max": 1.0,
	})
	registry.register_ore_vein_rule({
		"key": "snt:ore_copper",
		"dimension": dim,
		"host_material_key": "snt:stone",
		"ore_material_key": "snt:ore_copper",
		"combined_min": 0.25,
		"combined_max": 0.5,
	})
	registry.register_ore_vein_rule({
		"key": "snt:ore_coal",
		"dimension": dim,
		"host_material_key": "snt:stone",
		"ore_material_key": "snt:ore_coal",
		"combined_min": 0.05,
		"combined_max": 0.25,
	})

	# Rock layer rules — same as overworld, scoped to this dimension.
	registry.register_rock_layer_rule({
		"key": "snt:granite",
		"dimension": dim,
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
		"dimension": dim,
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
		"dimension": dim,
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

	// --- Tree species materials ---

	// Oak: temperate deciduous, round canopy.
	registry.register_material({
		"id": MAT_OAK_WOOD,
		"key": "snt:oak_wood",
		"display_name": "Oak Wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.oak", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_OAK_LEAVES,
		"key": "snt:oak_leaves",
		"display_name": "Oak Leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_OAK_SAPLING,
		"key": "snt:oak_sapling",
		"display_name": "Oak Sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.oak", "count": 1 }],
	})

	// Birch: cold-temperate deciduous, column canopy.
	registry.register_material({
		"id": MAT_BIRCH_WOOD,
		"key": "snt:birch_wood",
		"display_name": "Birch Wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 0.8,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.birch", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_BIRCH_LEAVES,
		"key": "snt:birch_leaves",
		"display_name": "Birch Leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.15,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_BIRCH_SAPLING,
		"key": "snt:birch_sapling",
		"display_name": "Birch Sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.birch", "count": 1 }],
	})

	// Spruce: cold evergreen, cone canopy.
	registry.register_material({
		"id": MAT_SPRUCE_WOOD,
		"key": "snt:spruce_wood",
		"display_name": "Spruce Wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.spruce", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_SPRUCE_LEAVES,
		"key": "snt:spruce_leaves",
		"display_name": "Spruce Leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_SPRUCE_SAPLING,
		"key": "snt:spruce_sapling",
		"display_name": "Spruce Sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.spruce", "count": 1 }],
	})

	// Acacia: tropical deciduous, umbrella canopy.
	registry.register_material({
		"id": MAT_ACACIA_WOOD,
		"key": "snt:acacia_wood",
		"display_name": "Acacia Wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 0.9,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.acacia", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_ACACIA_LEAVES,
		"key": "snt:acacia_leaves",
		"display_name": "Acacia Leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.15,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_ACACIA_SAPLING,
		"key": "snt:acacia_sapling",
		"display_name": "Acacia Sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.acacia", "count": 1 }],
	})

	// Maple: temperate deciduous, sphere canopy, vivid autumn color.
	registry.register_material({
		"id": MAT_MAPLE_WOOD,
		"key": "snt:maple_wood",
		"display_name": "Maple Wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.maple", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_MAPLE_LEAVES,
		"key": "snt:maple_leaves",
		"display_name": "Maple Leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_MAPLE_SAPLING,
		"key": "snt:maple_sapling",
		"display_name": "Maple Sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.maple", "count": 1 }],
	})

	// Sequoia: warm-temperate evergreen, cone canopy, very tall.
	registry.register_material({
		"id": MAT_SEQUOIA_WOOD,
		"key": "snt:sequoia_wood",
		"display_name": "Sequoia Wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.sequoia", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_SEQUOIA_LEAVES,
		"key": "snt:sequoia_leaves",
		"display_name": "Sequoia Leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_SEQUOIA_SAPLING,
		"key": "snt:sequoia_sapling",
		"display_name": "Sequoia Sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.sequoia", "count": 1 }],
	})

	// Cherry: temperate deciduous, sphere canopy, fruit-bearing.
	registry.register_material({
		"id": MAT_CHERRY_WOOD,
		"key": "snt:cherry_wood",
		"display_name": "Cherry Wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 0.7,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.cherry", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_CHERRY_LEAVES,
		"key": "snt:cherry_leaves",
		"display_name": "Cherry Leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.15,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_CHERRY_SAPLING,
		"key": "snt:cherry_sapling",
		"display_name": "Cherry Sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.cherry", "count": 1 }],
	})

	// Olive: warm-temperate evergreen, sphere canopy, fruit-bearing.
	registry.register_material({
		"id": MAT_OLIVE_WOOD,
		"key": "snt:olive_wood",
		"display_name": "Olive Wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.1,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.olive", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_OLIVE_LEAVES,
		"key": "snt:olive_leaves",
		"display_name": "Olive Leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_OLIVE_SAPLING,
		"key": "snt:olive_sapling",
		"display_name": "Olive Sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.olive", "count": 1 }],
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

		// Tree species visuals.
		{ "material_key": "snt:oak_wood", "dimension": "overworld",
		  "albedo_color": Color(0.45, 0.27, 0.12) },
		{ "material_key": "snt:oak_leaves", "dimension": "overworld",
		  "albedo_color": Color(0.21, 0.42, 0.15), "cull_disabled": true },
		{ "material_key": "snt:oak_sapling", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.55, 0.15), "cull_disabled": true },
		{ "material_key": "snt:birch_wood", "dimension": "overworld",
		  "albedo_color": Color(0.80, 0.78, 0.70) },
		{ "material_key": "snt:birch_leaves", "dimension": "overworld",
		  "albedo_color": Color(0.25, 0.55, 0.18), "cull_disabled": true },
		{ "material_key": "snt:birch_sapling", "dimension": "overworld",
		  "albedo_color": Color(0.35, 0.60, 0.20), "cull_disabled": true },
		{ "material_key": "snt:spruce_wood", "dimension": "overworld",
		  "albedo_color": Color(0.35, 0.22, 0.10) },
		{ "material_key": "snt:spruce_leaves", "dimension": "overworld",
		  "albedo_color": Color(0.10, 0.30, 0.12), "cull_disabled": true },
		{ "material_key": "snt:spruce_sapling", "dimension": "overworld",
		  "albedo_color": Color(0.12, 0.35, 0.15), "cull_disabled": true },
		{ "material_key": "snt:acacia_wood", "dimension": "overworld",
		  "albedo_color": Color(0.55, 0.35, 0.15) },
		{ "material_key": "snt:acacia_leaves", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.55, 0.12), "cull_disabled": true },
		{ "material_key": "snt:acacia_sapling", "dimension": "overworld",
		  "albedo_color": Color(0.35, 0.55, 0.15), "cull_disabled": true },
		{ "material_key": "snt:maple_wood", "dimension": "overworld",
		  "albedo_color": Color(0.42, 0.25, 0.10) },
		{ "material_key": "snt:maple_leaves", "dimension": "overworld",
		  "albedo_color": Color(0.20, 0.50, 0.15), "cull_disabled": true },
		{ "material_key": "snt:maple_sapling", "dimension": "overworld",
		  "albedo_color": Color(0.28, 0.52, 0.18), "cull_disabled": true },
		{ "material_key": "snt:sequoia_wood", "dimension": "overworld",
		  "albedo_color": Color(0.40, 0.23, 0.10) },
		{ "material_key": "snt:sequoia_leaves", "dimension": "overworld",
		  "albedo_color": Color(0.12, 0.32, 0.10), "cull_disabled": true },
		{ "material_key": "snt:sequoia_sapling", "dimension": "overworld",
		  "albedo_color": Color(0.15, 0.38, 0.12), "cull_disabled": true },
		{ "material_key": "snt:cherry_wood", "dimension": "overworld",
		  "albedo_color": Color(0.50, 0.28, 0.22) },
		{ "material_key": "snt:cherry_leaves", "dimension": "overworld",
		  "albedo_color": Color(0.70, 0.35, 0.50), "cull_disabled": true },
		{ "material_key": "snt:cherry_sapling", "dimension": "overworld",
		  "albedo_color": Color(0.65, 0.30, 0.45), "cull_disabled": true },
		{ "material_key": "snt:olive_wood", "dimension": "overworld",
		  "albedo_color": Color(0.52, 0.40, 0.22) },
		{ "material_key": "snt:olive_leaves", "dimension": "overworld",
		  "albedo_color": Color(0.25, 0.38, 0.15), "cull_disabled": true },
		{ "material_key": "snt:olive_sapling", "dimension": "overworld",
		  "albedo_color": Color(0.28, 0.42, 0.18), "cull_disabled": true },
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

	// --- Tree species registration ---

	// Oak: temperate deciduous, round canopy, most common.
	registry.register_tree_species({
		"species_key": "oak",
		"display_name": "Oak",
		"temperature_min": -0.2,
		"temperature_max": 0.5,
		"humidity_min": 0.0,
		"humidity_max": 1.0,
		"density_weight": 1.2,
		"min_trunk_height": 3,
		"max_trunk_height": 5,
		"canopy_shape": CANOPY_SPHERE,
		"canopy_radius": 2,
		"wood_material_key": "snt:oak_wood",
		"leaves_material_key": "snt:oak_leaves",
		"sapling_material_key": "snt:oak_sapling",
		"is_evergreen": false,
		"ticks_to_young": 24000,
		"ticks_to_mature": 48000,
		"has_fruit": false,
		"wood_color": Color(0.45, 0.27, 0.12),
		"leaves_color": Color(0.21, 0.42, 0.15),
		"autumn_color": Color(0.75, 0.50, 0.12),
	})

	// Birch: cold-temperate deciduous, tall narrow canopy.
	registry.register_tree_species({
		"species_key": "birch",
		"display_name": "Birch",
		"temperature_min": -0.6,
		"temperature_max": 0.2,
		"humidity_min": 0.1,
		"humidity_max": 1.0,
		"density_weight": 0.8,
		"min_trunk_height": 5,
		"max_trunk_height": 8,
		"canopy_shape": CANOPY_COLUMN,
		"canopy_radius": 1,
		"wood_material_key": "snt:birch_wood",
		"leaves_material_key": "snt:birch_leaves",
		"sapling_material_key": "snt:birch_sapling",
		"is_evergreen": false,
		"ticks_to_young": 20000,
		"ticks_to_mature": 40000,
		"has_fruit": false,
		"wood_color": Color(0.80, 0.78, 0.70),
		"leaves_color": Color(0.25, 0.55, 0.18),
		"autumn_color": Color(0.85, 0.75, 0.15),
	})

	// Spruce: cold evergreen, cone canopy.
	registry.register_tree_species({
		"species_key": "spruce",
		"display_name": "Spruce",
		"temperature_min": -1.0,
		"temperature_max": -0.1,
		"humidity_min": 0.0,
		"humidity_max": 1.0,
		"density_weight": 1.0,
		"min_trunk_height": 4,
		"max_trunk_height": 7,
		"canopy_shape": CANOPY_CONE,
		"canopy_radius": 2,
		"wood_material_key": "snt:spruce_wood",
		"leaves_material_key": "snt:spruce_leaves",
		"sapling_material_key": "snt:spruce_sapling",
		"is_evergreen": true,
		"ticks_to_young": 30000,
		"ticks_to_mature": 60000,
		"has_fruit": false,
		"wood_color": Color(0.35, 0.22, 0.10),
		"leaves_color": Color(0.10, 0.30, 0.12),
		"autumn_color": Color(0.10, 0.30, 0.12),
	})

	// Acacia: tropical deciduous, umbrella canopy.
	registry.register_tree_species({
		"species_key": "acacia",
		"display_name": "Acacia",
		"temperature_min": 0.5,
		"temperature_max": 1.0,
		"humidity_min": -0.5,
		"humidity_max": 0.5,
		"density_weight": 0.7,
		"min_trunk_height": 3,
		"max_trunk_height": 5,
		"canopy_shape": CANOPY_UMBRELLA,
		"canopy_radius": 3,
		"wood_material_key": "snt:acacia_wood",
		"leaves_material_key": "snt:acacia_leaves",
		"sapling_material_key": "snt:acacia_sapling",
		"is_evergreen": false,
		"ticks_to_young": 28000,
		"ticks_to_mature": 56000,
		"has_fruit": false,
		"wood_color": Color(0.55, 0.35, 0.15),
		"leaves_color": Color(0.30, 0.55, 0.12),
		"autumn_color": Color(0.70, 0.55, 0.10),
	})

	// Maple: temperate deciduous, vivid autumn red.
	registry.register_tree_species({
		"species_key": "maple",
		"display_name": "Maple",
		"temperature_min": -0.3,
		"temperature_max": 0.4,
		"humidity_min": 0.1,
		"humidity_max": 1.0,
		"density_weight": 0.6,
		"min_trunk_height": 3,
		"max_trunk_height": 6,
		"canopy_shape": CANOPY_SPHERE,
		"canopy_radius": 2,
		"wood_material_key": "snt:maple_wood",
		"leaves_material_key": "snt:maple_leaves",
		"sapling_material_key": "snt:maple_sapling",
		"is_evergreen": false,
		"ticks_to_young": 26000,
		"ticks_to_mature": 52000,
		"has_fruit": false,
		"wood_color": Color(0.42, 0.25, 0.10),
		"leaves_color": Color(0.20, 0.50, 0.15),
		"autumn_color": Color(0.90, 0.25, 0.10),
	})

	// Sequoia: warm-temperate evergreen, very tall cone canopy.
	registry.register_tree_species({
		"species_key": "sequoia",
		"display_name": "Sequoia",
		"temperature_min": 0.1,
		"temperature_max": 0.6,
		"humidity_min": 0.3,
		"humidity_max": 1.0,
		"density_weight": 0.4,
		"min_trunk_height": 7,
		"max_trunk_height": 12,
		"canopy_shape": CANOPY_CONE,
		"canopy_radius": 3,
		"wood_material_key": "snt:sequoia_wood",
		"leaves_material_key": "snt:sequoia_leaves",
		"sapling_material_key": "snt:sequoia_sapling",
		"is_evergreen": true,
		"ticks_to_young": 40000,
		"ticks_to_mature": 80000,
		"has_fruit": false,
		"wood_color": Color(0.40, 0.23, 0.10),
		"leaves_color": Color(0.12, 0.32, 0.10),
		"autumn_color": Color(0.12, 0.32, 0.10),
	})

	// Cherry: temperate deciduous, pink blossoms, fruit-bearing.
	registry.register_tree_species({
		"species_key": "cherry",
		"display_name": "Cherry",
		"temperature_min": -0.1,
		"temperature_max": 0.5,
		"humidity_min": 0.2,
		"humidity_max": 1.0,
		"density_weight": 0.3,
		"min_trunk_height": 2,
		"max_trunk_height": 4,
		"canopy_shape": CANOPY_SPHERE,
		"canopy_radius": 2,
		"wood_material_key": "snt:cherry_wood",
		"leaves_material_key": "snt:cherry_leaves",
		"sapling_material_key": "snt:cherry_sapling",
		"is_evergreen": false,
		"ticks_to_young": 22000,
		"ticks_to_mature": 44000,
		"has_fruit": true,
		"fruit_item_key": "fruit.cherry",
		"fruit_season": 2,
		"wood_color": Color(0.50, 0.28, 0.22),
		"leaves_color": Color(0.70, 0.35, 0.50),
		"autumn_color": Color(0.80, 0.30, 0.25),
	})

	// Olive: warm-temperate evergreen, fruit-bearing.
	registry.register_tree_species({
		"species_key": "olive",
		"display_name": "Olive",
		"temperature_min": 0.2,
		"temperature_max": 0.8,
		"humidity_min": -0.2,
		"humidity_max": 0.6,
		"density_weight": 0.3,
		"min_trunk_height": 2,
		"max_trunk_height": 4,
		"canopy_shape": CANOPY_SPHERE,
		"canopy_radius": 2,
		"wood_material_key": "snt:olive_wood",
		"leaves_material_key": "snt:olive_leaves",
		"sapling_material_key": "snt:olive_sapling",
		"is_evergreen": true,
		"ticks_to_young": 32000,
		"ticks_to_mature": 64000,
		"has_fruit": true,
		"fruit_item_key": "fruit.olive",
		"fruit_season": 1,
		"wood_color": Color(0.52, 0.40, 0.22),
		"leaves_color": Color(0.25, 0.38, 0.15),
		"autumn_color": Color(0.25, 0.38, 0.15),
	})


# Register runtime material IDs for blocks placed by players (not by terrain generation).
# These are separate from TerrainMaterialRoles because they are never consumed
# by any terrain pass. The command server uses these IDs to write terrain cells.
static func _register_runtime_material_ids(registry: Object) -> void:
	registry.set_runtime_material_ids({
		"ladder": "snt:ladder",
		"workbench": "snt:workbench",
	})
