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


static func create_default_config() -> GDWorldGenConfig:
	var registry := GDTerrainContentRegistry.new()
	_register_builtin_material_interactions(registry)
	_register_builtin_tile_mappings(registry)
	_register_builtin_generation_rules(registry)
	return registry.freeze()


static func _register_builtin_material_interactions(registry: GDTerrainContentRegistry) -> void:
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
		"flags": GDTerrainContentRegistry.FLAG_SOLID | GDTerrainContentRegistry.FLAG_MINEABLE,
		"hardness": 1.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "dust.stone", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_DIRT,
		"key": "snt:dirt",
		"display_name": "Dirt",
		"flags": GDTerrainContentRegistry.FLAG_WALKABLE | GDTerrainContentRegistry.FLAG_MINEABLE,
		"hardness": 0.5,
		"required_tool_tag": "shovel",
		"required_mining_level": 0,
		"drops": [{ "item_key": "tiny_dust.stone", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_SAND,
		"key": "snt:sand",
		"display_name": "Sand",
		"flags": GDTerrainContentRegistry.FLAG_WALKABLE | GDTerrainContentRegistry.FLAG_MINEABLE,
		"hardness": 0.45,
		"required_tool_tag": "shovel",
		"required_mining_level": 0,
		"drops": [{ "item_key": "tiny_dust.stone", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_WATER,
		"key": "snt:water",
		"display_name": "Water",
		"flags": GDTerrainContentRegistry.FLAG_LIQUID,
		"hardness": 100.0,
	})
	registry.register_material({
		"id": MAT_LAVA,
		"key": "snt:lava",
		"display_name": "Lava",
		"flags": GDTerrainContentRegistry.FLAG_LIQUID,
		"hardness": 100.0,
	})
	registry.register_material({
		"id": MAT_ORE_IRON,
		"key": "snt:ore_iron",
		"display_name": "Iron Ore",
		"flags": GDTerrainContentRegistry.FLAG_SOLID | GDTerrainContentRegistry.FLAG_MINEABLE,
		"hardness": 2.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.iron", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_ORE_COPPER,
		"key": "snt:ore_copper",
		"display_name": "Copper Ore",
		"flags": GDTerrainContentRegistry.FLAG_SOLID | GDTerrainContentRegistry.FLAG_MINEABLE,
		"hardness": 2.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.copper", "count": 1, "min_count": 1, "max_count": 2 }],
	})
	registry.register_material({
		"id": MAT_ORE_COAL,
		"key": "snt:ore_coal",
		"display_name": "Coal Ore",
		"flags": GDTerrainContentRegistry.FLAG_SOLID | GDTerrainContentRegistry.FLAG_MINEABLE,
		"hardness": 1.8,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "gem.coal", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_WOOD,
		"key": "snt:wood",
		"display_name": "Wood",
		"flags": GDTerrainContentRegistry.FLAG_SOLID | GDTerrainContentRegistry.FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "dust.wood", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_LEAVES,
		"key": "snt:leaves",
		"display_name": "Leaves",
		"flags": GDTerrainContentRegistry.FLAG_WALKABLE | GDTerrainContentRegistry.FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})


static func _register_builtin_tile_mappings(registry: GDTerrainContentRegistry) -> void:
	var mappings := [
		{ "material_key": "snt:air", "layer": "surface", "source_id": 0, "atlas": Vector2i(0, 0), "variant_count": 1, "enabled": false },
		{ "material_key": "snt:air", "layer": "underground", "source_id": 0, "atlas": Vector2i(0, 0), "variant_count": 1, "enabled": false },
		{ "material_key": "snt:stone", "layer": "surface", "source_id": 0, "atlas": Vector2i(4, 0), "variant_count": 1 },
		{ "material_key": "snt:stone", "layer": "underground", "source_id": 0, "atlas": Vector2i(0, 5), "variant_count": 12 },
		{ "material_key": "snt:dirt", "layer": "surface", "source_id": 0, "atlas": Vector2i(0, 0), "variant_count": 4 },
		{ "material_key": "snt:dirt", "layer": "underground", "source_id": 0, "atlas": Vector2i(0, 3), "variant_count": 3 },
		{ "material_key": "snt:sand", "layer": "surface", "source_id": 0, "atlas": Vector2i(0, 1), "variant_count": 4 },
		{ "material_key": "snt:sand", "layer": "underground", "source_id": 0, "atlas": Vector2i(6, 4), "variant_count": 4 },
		{ "material_key": "snt:water", "layer": "surface", "source_id": 0, "atlas": Vector2i(8, 0), "variant_count": 1 },
		{ "material_key": "snt:water", "layer": "underground", "source_id": 0, "atlas": Vector2i(8, 6), "variant_count": 1 },
		{ "material_key": "snt:lava", "layer": "surface", "source_id": 0, "atlas": Vector2i(9, 0), "variant_count": 1 },
		{ "material_key": "snt:lava", "layer": "underground", "source_id": 0, "atlas": Vector2i(7, 6), "variant_count": 1 },
		{ "material_key": "snt:ore_iron", "layer": "surface", "source_id": 0, "atlas": Vector2i(5, 0), "variant_count": 1 },
		{ "material_key": "snt:ore_iron", "layer": "underground", "source_id": 0, "atlas": Vector2i(4, 3), "variant_count": 1 },
		{ "material_key": "snt:ore_copper", "layer": "surface", "source_id": 0, "atlas": Vector2i(6, 0), "variant_count": 1 },
		{ "material_key": "snt:ore_copper", "layer": "underground", "source_id": 0, "atlas": Vector2i(5, 3), "variant_count": 1 },
		{ "material_key": "snt:ore_coal", "layer": "surface", "source_id": 0, "atlas": Vector2i(7, 0), "variant_count": 1 },
		{ "material_key": "snt:ore_coal", "layer": "underground", "source_id": 0, "atlas": Vector2i(6, 3), "variant_count": 1 },
		{ "material_key": "snt:wood", "layer": "surface", "source_id": 0, "atlas": Vector2i(10, 0), "variant_count": 1 },
		{ "material_key": "snt:wood", "layer": "underground", "source_id": 0, "atlas": Vector2i(10, 0), "variant_count": 1 },
		{ "material_key": "snt:leaves", "layer": "surface", "source_id": 0, "atlas": Vector2i(11, 0), "variant_count": 1 },
		{ "material_key": "snt:leaves", "layer": "underground", "source_id": 0, "atlas": Vector2i(11, 0), "variant_count": 1 },
	]
	for mapping in mappings:
		registry.register_tile_mapping(mapping)


static func _register_builtin_generation_rules(registry: GDTerrainContentRegistry) -> void:
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
	})

	registry.register_base_terrain_rule({
		"layer": "surface",
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
	registry.register_base_terrain_rule({
		"layer": "underground",
		"mode": "caves",
		"default_material_key": "snt:stone",
		"cave_air_material_key": "snt:air",
		"cave_scale": 0.04,
		"cave_octaves": 4,
		"cave_threshold": 0.35,
		"cave_edge_threshold_add": 0.25,
	})

	registry.register_biome_rule({
		"key": "snt:desert_sand",
		"layer": "surface",
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:sand",
		"temperature_min": 0.3,
		"humidity_max": -0.2,
	})
	registry.register_biome_rule({
		"key": "snt:beach_sand",
		"layer": "surface",
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:sand",
		"requires_near_material": true,
		"near_material_key": "snt:water",
		"near_radius": 2,
	})
	registry.register_biome_rule({
		"key": "snt:rocky_highlands",
		"layer": "surface",
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:stone",
		"temperature_max": -0.4,
		"humidity_max": -0.1,
	})
	registry.register_biome_rule({
		"key": "snt:cave_dirt_floor",
		"layer": "underground",
		"source_material_key": "snt:air",
		"result_material_key": "snt:dirt",
		"temperature_min": 0.1,
		"humidity_min": 0.0,
		"requires_floor_support": true,
		"support_material_key": "snt:stone",
		"detail_scale": 0.1,
		"detail_octaves": 2,
		"detail_threshold": 0.2,
	})
	registry.register_biome_rule({
		"key": "snt:dry_cave_sand",
		"layer": "underground",
		"source_material_key": "snt:air",
		"result_material_key": "snt:sand",
		"temperature_max": -0.2,
		"humidity_max": -0.1,
		"requires_floor_support": true,
		"support_material_key": "snt:stone",
		"detail_scale": 0.08,
		"detail_octaves": 2,
		"detail_threshold": 0.3,
	})

	registry.register_ore_vein_rule({
		"key": "snt:ore_iron",
		"layer": "underground",
		"host_material_key": "snt:stone",
		"ore_material_key": "snt:ore_iron",
		"combined_min": 0.5,
		"combined_max": 1.0,
	})
	registry.register_ore_vein_rule({
		"key": "snt:ore_copper",
		"layer": "underground",
		"host_material_key": "snt:stone",
		"ore_material_key": "snt:ore_copper",
		"combined_min": 0.25,
		"combined_max": 0.5,
	})
	registry.register_ore_vein_rule({
		"key": "snt:ore_coal",
		"layer": "underground",
		"host_material_key": "snt:stone",
		"ore_material_key": "snt:ore_coal",
		"combined_min": 0.05,
		"combined_max": 0.25,
	})
