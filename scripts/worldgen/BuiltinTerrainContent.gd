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

const FLAG_WALKABLE := 1
const FLAG_SOLID := 2
const FLAG_LIQUID := 4
const FLAG_MINEABLE := 8
const FLAG_CLIMBABLE := 16


static func create_default_config() -> Resource:
	var registry: Object = ClassDB.instantiate("GDTerrainContentRegistry")
	if registry == null:
		push_error("BuiltinTerrainContent: GDTerrainContentRegistry is not registered.")
		return null
	_register_builtin_material_interactions(registry)
	_register_builtin_tile_mappings(registry)
	_register_builtin_generation_rules(registry)
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
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
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
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
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


static func _register_builtin_tile_mappings(registry: Object) -> void:
	var mappings := [
		{ "material_key": "snt:air", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(0, 0), "variant_count": 1, "enabled": false },
		{ "material_key": "snt:stone", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(4, 0), "variant_count": 1 },
		{ "material_key": "snt:dirt", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(0, 0), "variant_count": 4 },
		{ "material_key": "snt:sand", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(0, 1), "variant_count": 4 },
		{ "material_key": "snt:water", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(8, 0), "variant_count": 1 },
		{ "material_key": "snt:lava", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(9, 0), "variant_count": 1 },
		{ "material_key": "snt:ore_iron", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(5, 0), "variant_count": 1 },
		{ "material_key": "snt:ore_copper", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(6, 0), "variant_count": 1 },
		{ "material_key": "snt:ore_coal", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(7, 0), "variant_count": 1 },
		{ "material_key": "snt:wood", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(10, 0), "variant_count": 1 },
		{ "material_key": "snt:leaves", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(11, 0), "variant_count": 1 },
		{ "material_key": "snt:ladder", "dimension": "overworld", "source_id": 0, "atlas": Vector2i(12, 0), "variant_count": 1 },
	]
	for mapping in mappings:
		registry.register_tile_mapping(mapping)


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
		# Ladder is NOT generated by any terrain pass; it is placed by players
		# at runtime. The role entry exists so C++ code can resolve the
		# material ID via TerrainMaterialRoles / MaterialIds.
		"ladder_key": "snt:ladder",
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

	# Planet configuration: spherical world with radius 512 blocks.
	# Center is placed so that the player starts near the "north pole" surface.
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
	})
