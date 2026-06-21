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

# Tree species materials: wood, leaves, sapling per species.
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

# Ore materials: metals, non-metals, and gemstones.
# Based on the periodic table and GT mod vein group design.
const MAT_ORE_TIN := 39
const MAT_ORE_ZINC := 40
const MAT_ORE_LEAD := 41
const MAT_ORE_SILVER := 42
const MAT_ORE_GOLD := 43
const MAT_ORE_NICKEL := 44
const MAT_ORE_BAUXITE := 45
const MAT_ORE_MANGANESE := 46
const MAT_ORE_TUNGSTEN := 47
const MAT_ORE_TITANIUM := 48
const MAT_ORE_PLATINUM := 49
const MAT_ORE_COBALT := 50
const MAT_ORE_URANIUM := 51
const MAT_ORE_SULFUR := 52
const MAT_ORE_DIAMOND := 53
const MAT_ORE_RUBY := 54
const MAT_ORE_SAPPHIRE := 55
const MAT_ORE_EMERALD := 56
const MAT_ORE_SALT := 57
const MAT_ORE_FLUORITE := 58
const MAT_ORE_GRAPHITE := 59
const MAT_ORE_PYRITE := 60
const MAT_ORE_GALENA := 61
const MAT_ORE_CINNABAR := 62
const MAT_ORE_MAGNETITE := 63
const MAT_ORE_CASSITERITE := 64
const MAT_ORE_ILMENITE := 65
const MAT_ORE_CHALCOPYRITE := 66
const MAT_ORE_SPHALERITE := 67
const MAT_ORE_PENTLANDITE := 68

# Planetary rock material IDs �?each yields unique dust when mined.
# These are separate TerrainMaterial entries from the generic snt:stone.
const MAT_GRANITE     := 69
const MAT_BASALT      := 70
const MAT_MARBLE      := 71
const MAT_SANDSTONE   := 72
const MAT_SHALE       := 73
const MAT_KOMATIITE   := 74
const MAT_REGOLITH    := 75
const MAT_ANORTHOSTIE := 76

# Player-placed fence block for captive creature husbandry (enclosures).
const MAT_FENCE := 77

# Farmland and crop stage materials (Tier 1 planting system).
# Farmland is the tilled dirt block; crop stages are 4 per species.
const MAT_FARMLAND := 78
const MAT_WHEAT_SEED := 79
const MAT_WHEAT_SPROUT := 80
const MAT_WHEAT_GROWING := 81
const MAT_WHEAT_MATURE := 82
const MAT_CARROT_SEED := 83
const MAT_CARROT_SPROUT := 84
const MAT_CARROT_GROWING := 85
const MAT_CARROT_MATURE := 86
const MAT_POTATO_SEED := 87
const MAT_POTATO_SPROUT := 88
const MAT_POTATO_GROWING := 89
const MAT_POTATO_MATURE := 90
const MAT_COTTON_SEED := 91
const MAT_COTTON_SPROUT := 92
const MAT_COTTON_GROWING := 93
const MAT_COTTON_MATURE := 94
const MAT_HERB_SEED := 95
const MAT_HERB_SPROUT := 96
const MAT_HERB_GROWING := 97
const MAT_HERB_MATURE := 98
const MAT_PUMPKIN_SEED := 99
const MAT_PUMPKIN_SPROUT := 100
const MAT_PUMPKIN_GROWING := 101
const MAT_PUMPKIN_MATURE := 102
const MAT_SNOW := 103
const MAT_ICE := 104

# Crop category enum (must match C++ CropCategory).
const CROP_GRAIN := 0
const CROP_ROOT := 1
const CROP_FIBER := 2
const CROP_HERB := 3
const CROP_FRUIT := 4
const CROP_MAGIC := 5

# Season enum (must match SeasonSystem: 0=Spring, 1=Summer, 2=Autumn, 3=Winter, -1=Any).
const SEASON_ANY := -1
const SEASON_SPRING := 0
const SEASON_SUMMER := 1
const SEASON_AUTUMN := 2
const SEASON_WINTER := 3

# Canopy shape enum (must match C++ CanopyShape).
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


# Create a multi-planet config from an array of PlanetDescriptor resources.
# Registers base terrain rules, biome rules, ore vein rules, rock layer rules,
# and planet configs for each landable planet. Stars are skipped.
# All planets share the same material set and biome/ore/rock rules,
# but each planet has its own dimension_id and PlanetConfig.
static func create_config_for_universe(universe_planets: Array[PlanetDescriptor]) -> Resource:
	var registry: GDTerrainContentRegistry = ClassDB.instantiate("GDTerrainContentRegistry")
	if registry == null:
		push_error("BuiltinTerrainContent: GDTerrainContentRegistry is not registered.")
		return null
	_register_builtin_material_interactions(registry)
	_register_builtin_material_roles(registry)
	_register_builtin_material_visuals(registry)
	_register_tree_species(registry)
	_register_crop_species(registry)
	_register_runtime_material_ids(registry)

	# Register generation rules and planet configs for each landable planet.
	var registered_count := 0
	for planet in universe_planets:
		if planet.is_star:
			continue
		_register_planet_generation_rules(registry, planet)
		registry.register_planet_config(planet.to_planet_config_dict())
		registered_count += 1
		print("[TerrainContent] registered planet: dim=%s center=%s radius=%.1f" % [planet.dimension_id, planet.local_center, planet.planet_radius])
	print("[TerrainContent] create_config_for_universe: total_planets=%d registered=%d" % [universe_planets.size(), registered_count])

	return registry.freeze()


# Register generation rules for a single planet dimension.
# Rules are differentiated based on planet properties:
#   - radius: affects rock layer depth range
#   - gravity_multiplier: affects collapse chance
#   - sea_level_fraction: affects water-related biomes
#   - atmosphere_type: affects special biome/rock rules
#   - cave_threshold: affects cave density in base terrain
static func _register_planet_generation_rules(
		registry: GDTerrainContentRegistry, planet: PlanetDescriptor) -> void:
	var dim := String(planet.dimension_id)
	var radius := planet.planet_radius
	var gravity := planet.gravity_multiplier
	var has_water := planet.sea_level_fraction > 0.01
	var atmo_type: int = planet.atmosphere_type

	# --- Base terrain rule ---
	# Adjust water elevation and stone thresholds based on planet properties.
	# Planets without water use extreme thresholds so water never appears.
	# Cave density is derived from the planet's cave_threshold parameter.
	var water_elev_max := -0.25 if has_water else -999.0
	var water_detail_max := 0.3 if has_water else -999.0
	# Stone exposure: higher gravity = more exposed rock (steeper terrain).
	var stone_abs_min := 0.55 if gravity < 1.5 else 0.45
	registry.register_base_terrain_rule({
		"dimension": dim,
		"mode": "surface_elevation",
		"default_material_key": "snt:dirt",
		"low_elevation_material_key": "snt:water" if has_water else "snt:stone",
		"high_elevation_material_key": "snt:stone",
		"elevation_scale": 0.02,
		"elevation_octaves": 4,
		"detail_scale": 0.05,
		"detail_octaves": 3,
		"water_elevation_max": water_elev_max,
		"water_detail_max": water_detail_max,
		"stone_elevation_abs_min": stone_abs_min,
	})

	# --- Biome rules ---
	# Desert: always present on hot dry areas.
	# On thin/no-atmosphere planets, desert expands (wider temperature range).
	var desert_temp_min := 0.3
	if atmo_type == PlanetDescriptor.AtmosphereType.NONE or \
			atmo_type == PlanetDescriptor.AtmosphereType.THIN:
		desert_temp_min = 0.1
	registry.register_biome_rule({
		"key": "snt:desert_sand",
		"dimension": dim,
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:sand",
		"temperature_min": desert_temp_min,
		"humidity_max": -0.2,
	})
	# Beach sand: only on planets with water.
	if has_water:
		registry.register_biome_rule({
			"key": "snt:beach_sand",
			"dimension": dim,
			"source_material_key": "snt:dirt",
			"result_material_key": "snt:sand",
			"requires_near_material": true,
			"near_material_key": "snt:water",
			"near_radius": 2,
		})
	# Rocky highlands: always present on cold dry areas.
	# On toxic/corrosive planets, rock exposure expands (less vegetation).
	var rocky_temp_max := -0.4
	if atmo_type == PlanetDescriptor.AtmosphereType.TOXIC or \
			atmo_type == PlanetDescriptor.AtmosphereType.CORROSIVE:
		rocky_temp_max = -0.1
	registry.register_biome_rule({
		"key": "snt:rocky_highlands",
		"dimension": dim,
		"source_material_key": "snt:dirt",
		"result_material_key": "snt:stone",
		"temperature_max": rocky_temp_max,
		"humidity_max": -0.1,
	})
	# Barren wasteland: on toxic/corrosive planets, dirt becomes stone
	# even in moderate temperatures (no soil formation).
	if atmo_type == PlanetDescriptor.AtmosphereType.TOXIC or \
			atmo_type == PlanetDescriptor.AtmosphereType.CORROSIVE:
		registry.register_biome_rule({
			"key": "snt:barren_wasteland",
			"dimension": dim,
			"source_material_key": "snt:dirt",
			"result_material_key": "snt:stone",
			"temperature_min": -0.1,
			"temperature_max": 0.3,
			"humidity_max": 0.0,
		})

	# --- Rock type selection (must be before ore vein and rock layer rules) ---
	# Select primary and secondary rock types based on planet properties.
	# This is what makes each planet's underground composition unique.
	var primary_rock := "snt:granite_rock"   # default
	var secondary_rock := "snt:basalt_rock"   # default
	var deep_rock := "snt:deepstone"          # always deepstone for deep layers

	# Thin/no atmosphere planets: regolith or anorthosite surface rock.
	if atmo_type == PlanetDescriptor.AtmosphereType.NONE:
		primary_rock = "snt:anorthosite_rock"
		secondary_rock = "snt:regolith_rock"
	elif atmo_type == PlanetDescriptor.AtmosphereType.THIN:
		primary_rock = "snt:regolith_rock"
		secondary_rock = "snt:granite_rock"
	# High gravity planets: komatiite (ancient volcanic).
	elif gravity >= 1.8:
		primary_rock = "snt:komatiite_rock"
		secondary_rock = "snt:basalt_rock"
	# Water-bearing planets: shale + granite (sedimentary + igneous).
	elif has_water and gravity < 1.2:
		primary_rock = "snt:granite_rock"
		secondary_rock = "snt:shale_rock"
	# Desert/dry planets: sandstone + marble.
	elif not has_water and atmo_type == PlanetDescriptor.AtmosphereType.BREATHABLE:
		primary_rock = "snt:sandstone_rock"
		secondary_rock = "snt:marble_rock"
	# Toxic/corrosive: basalt + marble (chemically resistant).
	elif atmo_type == PlanetDescriptor.AtmosphereType.TOXIC or \
			atmo_type == PlanetDescriptor.AtmosphereType.CORROSIVE:
		primary_rock = "snt:basalt_rock"
		secondary_rock = "snt:marble_rock"

	# --- Rock layer rules ---
	# Depth range scales with planet radius.
	# Small planets have shallower crust layers.
	var crust_depth := mini(radius * 0.2, 100.0)
	var deep_start := crust_depth * 0.6

	# Primary rock layer: covers the largest noise range.
	var primary_collapse := 0.3 * gravity
	registry.register_rock_layer_rule({
		"key": "snt:primary_rock",
		"dimension": dim,
		"rock_material_key": primary_rock,
		"noise_scale": 0.005,
		"noise_octaves": 3,
		"noise_min": -1.0,
		"noise_max": 0.0,
		"depth_min": 0.0,
		"depth_max": crust_depth,
		"hardness_multiplier": 1.0,
		"collapse_chance": primary_collapse,
		"associated_ores": [
			"snt:ore_iron", "snt:ore_copper",
			"snt:ore_chalcopyrite", "snt:ore_cassiterite",
			"snt:ore_sphalerite", "snt:ore_galena",
			"snt:ore_magnetite", "snt:ore_bauxite",
			"snt:ore_salt", "snt:ore_sulfur",
			"snt:ore_tin", "snt:ore_zinc",
			"snt:ore_pyrite", "snt:ore_fluorite",
		] if radius >= 200.0 else [
			"snt:ore_iron", "snt:ore_chalcopyrite",
			"snt:ore_cassiterite", "snt:ore_salt",
			"snt:ore_tin", "snt:ore_pyrite",
		],
	})

	# Secondary rock layer: different composition, complementary noise range.
	if radius >= 250.0:
		var secondary_collapse := 0.25 * gravity
		registry.register_rock_layer_rule({
			"key": "snt:secondary_rock",
			"dimension": dim,
			"rock_material_key": secondary_rock,
			"noise_scale": 0.005,
			"noise_octaves": 3,
			"noise_min": 0.0,
			"noise_max": 1.0,
			"depth_min": 0.0,
			"depth_max": crust_depth,
			"hardness_multiplier": 1.2,
			"collapse_chance": secondary_collapse,
			"associated_ores": [
				"snt:ore_iron", "snt:ore_magnetite",
				"snt:ore_pentlandite", "snt:ore_ilmenite",
				"snt:ore_nickel", "snt:ore_cinnabar",
				"snt:ore_manganese",
			],
		})

	# Deep rock layer: deepstone, always present.
	var deep_collapse := 0.5 * gravity
	registry.register_rock_layer_rule({
		"key": "snt:deeprock",
		"dimension": dim,
		"rock_material_key": deep_rock,
		"noise_scale": 0.003,
		"noise_octaves": 2,
		"noise_min": -1.0,
		"noise_max": 1.0,
		"depth_min": deep_start,
		"depth_max": 10000.0,
		"hardness_multiplier": 1.5,
		"collapse_chance": deep_collapse,
		"associated_ores": [
			"snt:ore_tungsten", "snt:ore_titanium",
			"snt:ore_uranium", "snt:ore_platinum",
			"snt:ore_cobalt", "snt:ore_diamond",
			"snt:ore_ruby", "snt:ore_sapphire",
			"snt:ore_emerald", "snt:ore_graphite",
		],
	})


static func _register_builtin_material_interactions(registry: GDTerrainContentRegistry) -> void:
	registry.register_material({
		"id": MAT_AIR,
		"key": "snt:air",
		"title_key": "terrain.air",
		"flags": 0,
		"hardness": 0.0,
	})
	registry.register_material({
		"id": MAT_STONE,
		"key": "snt:stone",
		"title_key": "terrain.stone",
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
		"title_key": "terrain.dirt",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.5,
		"required_tool_tag": "shovel",
		"required_mining_level": 0,
		"drops": [{ "item_key": "tiny_dust.stone", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_SAND,
		"key": "snt:sand",
		"title_key": "terrain.sand",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE | FLAG_GRAVITY_FALL,
		"hardness": 0.45,
		"required_tool_tag": "shovel",
		"required_mining_level": 0,
		"drops": [{ "item_key": "tiny_dust.stone", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_WATER,
		"key": "snt:water",
		"title_key": "terrain.water",
		"flags": FLAG_LIQUID,
		"hardness": 100.0,
	})
	registry.register_material({
		"id": MAT_LAVA,
		"key": "snt:lava",
		"title_key": "terrain.lava",
		"flags": FLAG_LIQUID,
		"hardness": 100.0,
	})
	registry.register_material({
		"id": MAT_SNOW,
		"key": "snt:snow",
		"title_key": "terrain.snow",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "shovel",
		"required_mining_level": 0,
	})
	registry.register_material({
		"id": MAT_ICE,
		"key": "snt:ice",
		"title_key": "terrain.ice",
		"flags": FLAG_SOLID | FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.6,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 0,
	})
	registry.register_material({
		"id": MAT_ORE_IRON,
		"key": "snt:ore_iron",
		"title_key": "terrain.iron_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.iron", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_ORE_COPPER,
		"key": "snt:ore_copper",
		"title_key": "terrain.copper_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.copper", "count": 1, "min_count": 1, "max_count": 2 }],
	})
	registry.register_material({
		"id": MAT_ORE_COAL,
		"key": "snt:ore_coal",
		"title_key": "terrain.coal_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.8,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "gem.coal", "count": 1 }],
	})

	# --- Basic metal ores ---

	# Tin: common shallow ore, used for bronze alloy.
	registry.register_material({
		"id": MAT_ORE_TIN,
		"key": "snt:ore_tin",
		"title_key": "terrain.tin_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.tin", "count": 1 }],
	})
	# Zinc: common shallow ore, used for brass alloy.
	registry.register_material({
		"id": MAT_ORE_ZINC,
		"key": "snt:ore_zinc",
		"title_key": "terrain.zinc_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.zinc", "count": 1 }],
	})
	# Lead: common mid-depth ore, heavy metal.
	registry.register_material({
		"id": MAT_ORE_LEAD,
		"key": "snt:ore_lead",
		"title_key": "terrain.lead_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.2,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.lead", "count": 1 }],
	})

	# --- Precious metal ores ---

	# Silver: mid-depth precious metal.
	registry.register_material({
		"id": MAT_ORE_SILVER,
		"key": "snt:ore_silver",
		"title_key": "terrain.silver_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.8,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.silver", "count": 1 }],
	})
	# Gold: mid-deep precious metal.
	registry.register_material({
		"id": MAT_ORE_GOLD,
		"key": "snt:ore_gold",
		"title_key": "terrain.gold_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 3.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.gold", "count": 1 }],
	})

	# --- Alloy metal ores ---

	# Nickel: mid-depth ore for stainless steel and invar.
	registry.register_material({
		"id": MAT_ORE_NICKEL,
		"key": "snt:ore_nickel",
		"title_key": "terrain.nickel_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.8,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.nickel", "count": 1 }],
	})
	# Bauxite: shallow ore for aluminum alloys.
	registry.register_material({
		"id": MAT_ORE_BAUXITE,
		"key": "snt:ore_bauxite",
		"title_key": "terrain.bauxite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.bauxite", "count": 1 }],
	})
	# Manganese: mid-depth ore for steel alloys.
	registry.register_material({
		"id": MAT_ORE_MANGANESE,
		"key": "snt:ore_manganese",
		"title_key": "terrain.manganese_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.manganese", "count": 1 }],
	})
	# Tungsten: deep ore for hard alloys.
	registry.register_material({
		"id": MAT_ORE_TUNGSTEN,
		"key": "snt:ore_tungsten",
		"title_key": "terrain.tungsten_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 4.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 3,
		"drops": [{ "item_key": "crushed.tungsten", "count": 1 }],
	})
	# Titanium: deep ore for advanced alloys.
	registry.register_material({
		"id": MAT_ORE_TITANIUM,
		"key": "snt:ore_titanium",
		"title_key": "terrain.titanium_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 4.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 3,
		"drops": [{ "item_key": "crushed.titanium", "count": 1 }],
	})

	# --- Rare metal ores ---

	# Platinum: very deep, extremely rare catalyst metal.
	registry.register_material({
		"id": MAT_ORE_PLATINUM,
		"key": "snt:ore_platinum",
		"title_key": "terrain.platinum_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 5.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 4,
		"drops": [{ "item_key": "crushed.platinum", "count": 1 }],
	})
	# Cobalt: deep ore for superalloys.
	registry.register_material({
		"id": MAT_ORE_COBALT,
		"key": "snt:ore_cobalt",
		"title_key": "terrain.cobalt_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 3.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 3,
		"drops": [{ "item_key": "crushed.cobalt", "count": 1 }],
	})

	# --- Energy ores ---

	# Uranium: very deep, extremely rare nuclear fuel.
	registry.register_material({
		"id": MAT_ORE_URANIUM,
		"key": "snt:ore_uranium",
		"title_key": "terrain.uranium_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 5.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 4,
		"drops": [{ "item_key": "crushed.uranium", "count": 1 }],
	})
	# Sulfur: volcanic zone ore for chemical processing.
	registry.register_material({
		"id": MAT_ORE_SULFUR,
		"key": "snt:ore_sulfur",
		"title_key": "terrain.sulfur_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "dust.sulfur", "count": 1 }],
	})

	# --- Gemstone ores ---

	# Diamond: very deep, extremely rare industrial/cutting gem.
	registry.register_material({
		"id": MAT_ORE_DIAMOND,
		"key": "snt:ore_diamond",
		"title_key": "terrain.diamond_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 5.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 4,
		"drops": [{ "item_key": "gem.diamond", "count": 1 }],
	})
	# Ruby: deep chromium-based gemstone.
	registry.register_material({
		"id": MAT_ORE_RUBY,
		"key": "snt:ore_ruby",
		"title_key": "terrain.ruby_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 4.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 3,
		"drops": [{ "item_key": "gem.ruby", "count": 1 }],
	})
	# Sapphire: deep aluminum-based gemstone.
	registry.register_material({
		"id": MAT_ORE_SAPPHIRE,
		"key": "snt:ore_sapphire",
		"title_key": "terrain.sapphire_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 4.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 3,
		"drops": [{ "item_key": "gem.sapphire", "count": 1 }],
	})
	# Emerald: deep beryllium-based gemstone.
	registry.register_material({
		"id": MAT_ORE_EMERALD,
		"key": "snt:ore_emerald",
		"title_key": "terrain.emerald_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 4.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 3,
		"drops": [{ "item_key": "gem.emerald", "count": 1 }],
	})

	# --- Non-metal / industrial ores ---

	# Salt: shallow-mid depth, food and chemical industry.
	registry.register_material({
		"id": MAT_ORE_SALT,
		"key": "snt:ore_salt",
		"title_key": "terrain.salt_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.2,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "dust.salt", "count": 1, "min_count": 1, "max_count": 3 }],
	})
	# Fluorite: mid-depth smelting flux.
	registry.register_material({
		"id": MAT_ORE_FLUORITE,
		"key": "snt:ore_fluorite",
		"title_key": "terrain.fluorite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "dust.fluorite", "count": 1 }],
	})
	# Graphite: mid-deep carbon material.
	registry.register_material({
		"id": MAT_ORE_GRAPHITE,
		"key": "snt:ore_graphite",
		"title_key": "terrain.graphite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.8,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "dust.graphite", "count": 1 }],
	})

	# --- GT-style mineral ores (multi-element minerals) ---

	# Pyrite: iron disulfide FeS2, common in many vein groups.
	registry.register_material({
		"id": MAT_ORE_PYRITE,
		"key": "snt:ore_pyrite",
		"title_key": "terrain.pyrite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.pyrite", "count": 1 }],
	})
	# Galena: lead sulfide PbS, lead-silver bearing mineral.
	registry.register_material({
		"id": MAT_ORE_GALENA,
		"key": "snt:ore_galena",
		"title_key": "terrain.galena_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.3,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.galena", "count": 1 }],
	})
	# Cinnabar: mercury sulfide HgS, volcanic zones.
	registry.register_material({
		"id": MAT_ORE_CINNABAR,
		"key": "snt:ore_cinnabar",
		"title_key": "terrain.cinnabar_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.8,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "dust.cinnabar", "count": 1 }],
	})
	# Magnetite: iron oxide Fe3O4, primary iron ore in GT veins.
	registry.register_material({
		"id": MAT_ORE_MAGNETITE,
		"key": "snt:ore_magnetite",
		"title_key": "terrain.magnetite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 3.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.magnetite", "count": 1 }],
	})
	# Cassiterite: tin oxide SnO2, primary tin ore.
	registry.register_material({
		"id": MAT_ORE_CASSITERITE,
		"key": "snt:ore_cassiterite",
		"title_key": "terrain.cassiterite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.cassiterite", "count": 1 }],
	})
	# Ilmenite: iron-titanium oxide FeTiO3, titanium source.
	registry.register_material({
		"id": MAT_ORE_ILMENITE,
		"key": "snt:ore_ilmenite",
		"title_key": "terrain.ilmenite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 3.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 3,
		"drops": [{ "item_key": "crushed.ilmenite", "count": 1 }],
	})
	# Chalcopyrite: copper iron sulfide CuFeS2, primary copper ore.
	registry.register_material({
		"id": MAT_ORE_CHALCOPYRITE,
		"key": "snt:ore_chalcopyrite",
		"title_key": "terrain.chalcopyrite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.2,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.chalcopyrite", "count": 1 }],
	})
	# Sphalerite: zinc sulfide ZnS, primary zinc ore.
	registry.register_material({
		"id": MAT_ORE_SPHALERITE,
		"key": "snt:ore_sphalerite",
		"title_key": "terrain.sphalerite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"drops": [{ "item_key": "crushed.sphalerite", "count": 1 }],
	})
	# Pentlandite: iron-nickel sulfide (Ni,Fe)9S8, primary nickel ore.
	registry.register_material({
		"id": MAT_ORE_PENTLANDITE,
		"key": "snt:ore_pentlandite",
		"title_key": "terrain.pentlandite_ore",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 2.8,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"drops": [{ "item_key": "crushed.pentlandite", "count": 1 }],
	})

	registry.register_material({
		"id": MAT_WOOD,
		"key": "snt:wood",
		"title_key": "terrain.wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "dust.wood", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_LEAVES,
		"key": "snt:leaves",
		"title_key": "terrain.leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_LADDER,
		"key": "snt:ladder",
		"title_key": "terrain.ladder",
		"flags": FLAG_MINEABLE | FLAG_CLIMBABLE,
		"hardness": 0.5,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "ladder", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_WORKBENCH,
		"key": "snt:workbench",
		"title_key": "terrain.workbench",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "workbench", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_FENCE,
		"key": "snt:fence",
		"title_key": "terrain.fence",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "fence", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_DEEPSTONE,
		"key": "snt:deepstone",
		"title_key": "terrain.deepstone",
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
		"title_key": "terrain.core_barrier",
		"flags": FLAG_SOLID | FLAG_INDESTRUCTIBLE,
		"hardness": -1.0,
	})

	# --- Planetary rock materials ---
	# Each rock type is a separate TerrainMaterial with unique drops.
	# Different planets use different rock compositions in their rock layers.

	# Granite: common crustal rock, standard hardness.
	registry.register_material({
		"id": MAT_GRANITE,
		"key": "snt:granite_rock",
		"title_key": "terrain.granite",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_COLLAPSE_RISK,
		"hardness": 1.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"collapse_chance": 0.3,
		"rock_layer_key": "snt:granite",
		"drops": [{ "item_key": "dust.granite", "count": 1 }],
	})
	# Basalt: harder volcanic rock, slightly more durable.
	registry.register_material({
		"id": MAT_BASALT,
		"key": "snt:basalt_rock",
		"title_key": "terrain.basalt",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_COLLAPSE_RISK,
		"hardness": 2.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"collapse_chance": 0.25,
		"rock_layer_key": "snt:basalt",
		"drops": [{ "item_key": "dust.basalt", "count": 1 }],
	})
	# Marble: metamorphic rock, medium hardness.
	registry.register_material({
		"id": MAT_MARBLE,
		"key": "snt:marble_rock",
		"title_key": "terrain.marble",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_COLLAPSE_RISK,
		"hardness": 1.3,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"collapse_chance": 0.2,
		"rock_layer_key": "snt:marble",
		"drops": [{ "item_key": "dust.marble", "count": 1 }],
	})
	# Sandstone: soft sedimentary rock, easy to mine.
	registry.register_material({
		"id": MAT_SANDSTONE,
		"key": "snt:sandstone_rock",
		"title_key": "terrain.sandstone",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_GRAVITY_FALL,
		"hardness": 0.8,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 0,
		"collapse_chance": 0.15,
		"rock_layer_key": "snt:sandstone",
		"drops": [{ "item_key": "dust.sandstone", "count": 1 }],
	})
	# Shale: sedimentary rock, tends to collapse.
	registry.register_material({
		"id": MAT_SHALE,
		"key": "snt:shale_rock",
		"title_key": "terrain.shale",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_COLLAPSE_RISK,
		"hardness": 1.0,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"collapse_chance": 0.4,
		"rock_layer_key": "snt:shale",
		"drops": [{ "item_key": "dust.shale", "count": 1 }],
	})
	# Komatiite: ancient volcanic rock, very hard.
	registry.register_material({
		"id": MAT_KOMATIITE,
		"key": "snt:komatiite_rock",
		"title_key": "terrain.komatiite",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_COLLAPSE_RISK,
		"hardness": 2.5,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 2,
		"collapse_chance": 0.35,
		"rock_layer_key": "snt:komatiite",
		"drops": [{ "item_key": "dust.komatiite", "count": 1 }],
	})
	# Regolith: weathered surface rock, soft and crumbly.
	registry.register_material({
		"id": MAT_REGOLITH,
		"key": "snt:regolith_rock",
		"title_key": "terrain.regolith",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_GRAVITY_FALL,
		"hardness": 0.6,
		"required_tool_tag": "shovel",
		"required_mining_level": 0,
		"collapse_chance": 0.1,
		"rock_layer_key": "snt:regolith",
		"drops": [{ "item_key": "dust.regolith", "count": 1 }],
	})
	# Anorthosite: highland crust rock, medium hardness.
	registry.register_material({
		"id": MAT_ANORTHOSTIE,
		"key": "snt:anorthosite_rock",
		"title_key": "terrain.anorthosite",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_COLLAPSE_RISK,
		"hardness": 1.4,
		"required_tool_tag": "pickaxe",
		"required_mining_level": 1,
		"collapse_chance": 0.2,
		"rock_layer_key": "snt:anorthosite",
		"drops": [{ "item_key": "dust.anorthosite", "count": 1 }],
	})

	# --- Tree species materials ---

	# Oak: temperate deciduous, round canopy.
	registry.register_material({
		"id": MAT_OAK_WOOD,
		"key": "snt:oak_wood",
		"title_key": "terrain.oak_wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.oak", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_OAK_LEAVES,
		"key": "snt:oak_leaves",
		"title_key": "terrain.oak_leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_OAK_SAPLING,
		"key": "snt:oak_sapling",
		"title_key": "terrain.oak_sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.oak", "count": 1 }],
	})

	# Birch: cold-temperate deciduous, column canopy.
	registry.register_material({
		"id": MAT_BIRCH_WOOD,
		"key": "snt:birch_wood",
		"title_key": "terrain.birch_wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 0.8,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.birch", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_BIRCH_LEAVES,
		"key": "snt:birch_leaves",
		"title_key": "terrain.birch_leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.15,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_BIRCH_SAPLING,
		"key": "snt:birch_sapling",
		"title_key": "terrain.birch_sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.birch", "count": 1 }],
	})

	# Spruce: cold evergreen, cone canopy.
	registry.register_material({
		"id": MAT_SPRUCE_WOOD,
		"key": "snt:spruce_wood",
		"title_key": "terrain.spruce_wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.spruce", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_SPRUCE_LEAVES,
		"key": "snt:spruce_leaves",
		"title_key": "terrain.spruce_leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_SPRUCE_SAPLING,
		"key": "snt:spruce_sapling",
		"title_key": "terrain.spruce_sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.spruce", "count": 1 }],
	})

	# Acacia: tropical deciduous, umbrella canopy.
	registry.register_material({
		"id": MAT_ACACIA_WOOD,
		"key": "snt:acacia_wood",
		"title_key": "terrain.acacia_wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 0.9,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.acacia", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_ACACIA_LEAVES,
		"key": "snt:acacia_leaves",
		"title_key": "terrain.acacia_leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.15,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_ACACIA_SAPLING,
		"key": "snt:acacia_sapling",
		"title_key": "terrain.acacia_sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.acacia", "count": 1 }],
	})

	# Maple: temperate deciduous, sphere canopy, vivid autumn color.
	registry.register_material({
		"id": MAT_MAPLE_WOOD,
		"key": "snt:maple_wood",
		"title_key": "terrain.maple_wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.maple", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_MAPLE_LEAVES,
		"key": "snt:maple_leaves",
		"title_key": "terrain.maple_leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_MAPLE_SAPLING,
		"key": "snt:maple_sapling",
		"title_key": "terrain.maple_sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.maple", "count": 1 }],
	})

	# Sequoia: warm-temperate evergreen, cone canopy, very tall.
	registry.register_material({
		"id": MAT_SEQUOIA_WOOD,
		"key": "snt:sequoia_wood",
		"title_key": "terrain.sequoia_wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.sequoia", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_SEQUOIA_LEAVES,
		"key": "snt:sequoia_leaves",
		"title_key": "terrain.sequoia_leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_SEQUOIA_SAPLING,
		"key": "snt:sequoia_sapling",
		"title_key": "terrain.sequoia_sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.sequoia", "count": 1 }],
	})

	# Cherry: temperate deciduous, sphere canopy, fruit-bearing.
	registry.register_material({
		"id": MAT_CHERRY_WOOD,
		"key": "snt:cherry_wood",
		"title_key": "terrain.cherry_wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 0.7,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.cherry", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_CHERRY_LEAVES,
		"key": "snt:cherry_leaves",
		"title_key": "terrain.cherry_leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.15,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_CHERRY_SAPLING,
		"key": "snt:cherry_sapling",
		"title_key": "terrain.cherry_sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.cherry", "count": 1 }],
	})

	# Olive: warm-temperate evergreen, sphere canopy, fruit-bearing.
	registry.register_material({
		"id": MAT_OLIVE_WOOD,
		"key": "snt:olive_wood",
		"title_key": "terrain.olive_wood",
		"flags": FLAG_SOLID | FLAG_MINEABLE,
		"hardness": 1.1,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "log.olive", "count": 1 }],
	})
	registry.register_material({
		"id": MAT_OLIVE_LEAVES,
		"key": "snt:olive_leaves",
		"title_key": "terrain.olive_leaves",
		"flags": FLAG_WALKABLE | FLAG_MINEABLE,
		"hardness": 0.2,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [],
	})
	registry.register_material({
		"id": MAT_OLIVE_SAPLING,
		"key": "snt:olive_sapling",
		"title_key": "terrain.olive_sapling",
		"flags": FLAG_MINEABLE,
		"hardness": 0.0,
		"required_tool_tag": "axe",
		"required_mining_level": 0,
		"drops": [{ "item_key": "sapling.olive", "count": 1 }],
	})

	# --- Farmland and crop stage materials (Tier 1 planting system) ---

	# Farmland: tilled dirt block. Breaking reverts to dirt (drops nothing,
	# the till command handles dirt→farmland conversion).
	registry.register_material({
		"id": MAT_FARMLAND,
		"key": "snt:farmland",
		"title_key": "terrain.farmland",
		"flags": FLAG_SOLID | FLAG_MINEABLE | FLAG_WALKABLE,
		"hardness": 0.5,
		"required_tool_tag": "shovel",
		"required_mining_level": 0,
		"drops": [],
	})

	# Crop stage materials: 6 species × 4 stages = 24 materials.
	# Non-mature stages drop seed; mature stage drops crop + seed.
	# Uses a data-driven loop to avoid 24 repetitive register_material calls.
	var _crop_stage_names := ["seed", "sprout", "growing", "mature"]
	var _crop_species_mats := [
		{"name": "wheat", "base_id": MAT_WHEAT_SEED,
				"seed_key": "seed.wheat", "crop_key": "crop.wheat"},
		{"name": "carrot", "base_id": MAT_CARROT_SEED,
				"seed_key": "seed.carrot", "crop_key": "crop.carrot"},
		{"name": "potato", "base_id": MAT_POTATO_SEED,
				"seed_key": "seed.potato", "crop_key": "crop.potato"},
		{"name": "cotton", "base_id": MAT_COTTON_SEED,
				"seed_key": "seed.cotton", "crop_key": "crop.cotton"},
		{"name": "herb", "base_id": MAT_HERB_SEED,
				"seed_key": "seed.herb", "crop_key": "crop.herb"},
		{"name": "pumpkin", "base_id": MAT_PUMPKIN_SEED,
				"seed_key": "seed.pumpkin", "crop_key": "crop.pumpkin"},
	]
	for sp in _crop_species_mats:
		for idx in range(4):
			var _stage: String = _crop_stage_names[idx]
			var _drops: Array = []
			if idx < 3:
				_drops = [{"item_key": sp["seed_key"], "count": 1}]
			else:
				_drops = [
					{"item_key": sp["crop_key"], "count": 1},
					{"item_key": sp["seed_key"], "count": 1},
				]
			registry.register_material({
				"id": sp["base_id"] + idx,
				"key": "snt:%s_%s" % [sp["name"], _stage],
				"title_key": "terrain.%s_%s" % [sp["name"], _stage],
				"flags": FLAG_WALKABLE | FLAG_MINEABLE,
				"hardness": 0.0,
				"required_mining_level": 0,
				"drops": _drops,
			})


static func _register_builtin_material_visuals(registry: GDTerrainContentRegistry) -> void:
	var visuals := [
		{ "material_key": "snt:air", "dimension": "overworld", "enabled": false,
		  "albedo_color": Color(0, 0, 0, 0) },
		{ "material_key": "snt:stone", "dimension": "overworld",
		  "albedo_color": Color(0.46, 0.47, 0.45) },
		{ "material_key": "snt:dirt", "dimension": "overworld",
		  "albedo_color": Color(0.33, 0.25, 0.14) },
		{ "material_key": "snt:sand", "dimension": "overworld",
		  "albedo_color": Color(0.73, 0.64, 0.40),
		  "sides_texture": "res://resource/terrain/sand/sand_tile_01_32.png",
		  "sides_variant_count": 4 },
		{ "material_key": "snt:water", "dimension": "overworld",
		  "albedo_color": Color(0.18, 0.39, 0.74, 0.78),
		  "transparent": true, "roughness": 0.1 },
		{ "material_key": "snt:lava", "dimension": "overworld",
		  "albedo_color": Color(0.95, 0.28, 0.08),
		  "emissive_color": Color(0.8, 0.2, 0.05), "roughness": 0.3 },
		{ "material_key": "snt:snow", "dimension": "overworld",
		  "albedo_color": Color(0.92, 0.95, 1.0), "roughness": 0.82 },
		{ "material_key": "snt:ice", "dimension": "overworld",
		  "albedo_color": Color(0.62, 0.82, 0.95, 0.82),
		  "transparent": true, "roughness": 0.12 },
		{ "material_key": "snt:ore_iron", "dimension": "overworld",
		  "albedo_color": Color(0.65, 0.58, 0.50) },
		{ "material_key": "snt:ore_copper", "dimension": "overworld",
		  "albedo_color": Color(0.72, 0.37, 0.18) },
		{ "material_key": "snt:ore_coal", "dimension": "overworld",
		  "albedo_color": Color(0.13, 0.13, 0.13) },

		# --- Basic metal ore visuals ---
		{ "material_key": "snt:ore_tin", "dimension": "overworld",
		  "albedo_color": Color(0.60, 0.60, 0.62) },
		{ "material_key": "snt:ore_zinc", "dimension": "overworld",
		  "albedo_color": Color(0.55, 0.58, 0.60) },
		{ "material_key": "snt:ore_lead", "dimension": "overworld",
		  "albedo_color": Color(0.40, 0.40, 0.42) },

		# --- Precious metal ore visuals ---
		{ "material_key": "snt:ore_silver", "dimension": "overworld",
		  "albedo_color": Color(0.80, 0.80, 0.82) },
		{ "material_key": "snt:ore_gold", "dimension": "overworld",
		  "albedo_color": Color(0.85, 0.70, 0.15) },

		# --- Alloy metal ore visuals ---
		{ "material_key": "snt:ore_nickel", "dimension": "overworld",
		  "albedo_color": Color(0.58, 0.58, 0.55) },
		{ "material_key": "snt:ore_bauxite", "dimension": "overworld",
		  "albedo_color": Color(0.72, 0.55, 0.38) },
		{ "material_key": "snt:ore_manganese", "dimension": "overworld",
		  "albedo_color": Color(0.45, 0.38, 0.35) },
		{ "material_key": "snt:ore_tungsten", "dimension": "overworld",
		  "albedo_color": Color(0.50, 0.50, 0.52) },
		{ "material_key": "snt:ore_titanium", "dimension": "overworld",
		  "albedo_color": Color(0.60, 0.62, 0.65) },

		# --- Rare metal ore visuals ---
		{ "material_key": "snt:ore_platinum", "dimension": "overworld",
		  "albedo_color": Color(0.78, 0.78, 0.80),
		  "emissive_color": Color(0.05, 0.05, 0.08), "roughness": 0.4 },
		{ "material_key": "snt:ore_cobalt", "dimension": "overworld",
		  "albedo_color": Color(0.25, 0.30, 0.65) },

		# --- Energy ore visuals ---
		{ "material_key": "snt:ore_uranium", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.55, 0.20),
		  "emissive_color": Color(0.08, 0.18, 0.03), "roughness": 0.5 },
		{ "material_key": "snt:ore_sulfur", "dimension": "overworld",
		  "albedo_color": Color(0.90, 0.85, 0.15) },

		# --- Gemstone ore visuals ---
		{ "material_key": "snt:ore_diamond", "dimension": "overworld",
		  "albedo_color": Color(0.70, 0.85, 0.90),
		  "emissive_color": Color(0.10, 0.15, 0.20), "roughness": 0.2 },
		{ "material_key": "snt:ore_ruby", "dimension": "overworld",
		  "albedo_color": Color(0.80, 0.12, 0.15),
		  "emissive_color": Color(0.15, 0.02, 0.03), "roughness": 0.3 },
		{ "material_key": "snt:ore_sapphire", "dimension": "overworld",
		  "albedo_color": Color(0.12, 0.18, 0.75),
		  "emissive_color": Color(0.02, 0.04, 0.12), "roughness": 0.3 },
		{ "material_key": "snt:ore_emerald", "dimension": "overworld",
		  "albedo_color": Color(0.10, 0.60, 0.25),
		  "emissive_color": Color(0.02, 0.10, 0.04), "roughness": 0.3 },

		# --- Non-metal / industrial ore visuals ---
		{ "material_key": "snt:ore_salt", "dimension": "overworld",
		  "albedo_color": Color(0.92, 0.90, 0.88) },
		{ "material_key": "snt:ore_fluorite", "dimension": "overworld",
		  "albedo_color": Color(0.55, 0.65, 0.75) },
		{ "material_key": "snt:ore_graphite", "dimension": "overworld",
		  "albedo_color": Color(0.22, 0.22, 0.24) },

		# --- GT-style mineral ore visuals ---
		{ "material_key": "snt:ore_pyrite", "dimension": "overworld",
		  "albedo_color": Color(0.72, 0.68, 0.30) },
		{ "material_key": "snt:ore_galena", "dimension": "overworld",
		  "albedo_color": Color(0.35, 0.35, 0.38) },
		{ "material_key": "snt:ore_cinnabar", "dimension": "overworld",
		  "albedo_color": Color(0.78, 0.18, 0.15) },
		{ "material_key": "snt:ore_magnetite", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.30, 0.32) },
		{ "material_key": "snt:ore_cassiterite", "dimension": "overworld",
		  "albedo_color": Color(0.55, 0.45, 0.30) },
		{ "material_key": "snt:ore_ilmenite", "dimension": "overworld",
		  "albedo_color": Color(0.38, 0.35, 0.32) },
		{ "material_key": "snt:ore_chalcopyrite", "dimension": "overworld",
		  "albedo_color": Color(0.70, 0.55, 0.20) },
		{ "material_key": "snt:ore_sphalerite", "dimension": "overworld",
		  "albedo_color": Color(0.50, 0.40, 0.22) },
		{ "material_key": "snt:ore_pentlandite", "dimension": "overworld",
		  "albedo_color": Color(0.55, 0.52, 0.35) },

		{ "material_key": "snt:wood", "dimension": "overworld",
		  "albedo_color": Color(0.45, 0.27, 0.12) },
		{ "material_key": "snt:leaves", "dimension": "overworld",
		  "albedo_color": Color(0.21, 0.42, 0.20), "cull_disabled": true },
		{ "material_key": "snt:ladder", "dimension": "overworld",
		  "albedo_color": Color(0.55, 0.30, 0.15), "cull_disabled": true },
		{ "material_key": "snt:workbench", "dimension": "overworld",
	  "albedo_color": Color(0.60, 0.40, 0.20) },
	{ "material_key": "snt:fence", "dimension": "overworld",
	  "albedo_color": Color(0.50, 0.32, 0.16), "cull_disabled": true },
	{ "material_key": "snt:deepstone", "dimension": "overworld",
	  "albedo_color": Color(0.30, 0.30, 0.32) },
		{ "material_key": "snt:core_barrier", "dimension": "overworld",
		  "albedo_color": Color(0.10, 0.0, 0.15),
		  "emissive_color": Color(0.15, 0.0, 0.25), "roughness": 0.5 },

		# Tree species visuals.
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

		# Farmland: dark brown tilled soil.
		{ "material_key": "snt:farmland", "dimension": "overworld",
		  "albedo_color": Color(0.25, 0.18, 0.10) },

		# Wheat stages: green→yellow.
		{ "material_key": "snt:wheat_seed", "dimension": "overworld",
		  "albedo_color": Color(0.35, 0.28, 0.10), "cull_disabled": true },
		{ "material_key": "snt:wheat_sprout", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.50, 0.15), "cull_disabled": true },
		{ "material_key": "snt:wheat_growing", "dimension": "overworld",
		  "albedo_color": Color(0.55, 0.60, 0.20), "cull_disabled": true },
		{ "material_key": "snt:wheat_mature", "dimension": "overworld",
		  "albedo_color": Color(0.85, 0.75, 0.25), "cull_disabled": true },

		# Carrot stages: green tops, orange root.
		{ "material_key": "snt:carrot_seed", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.25, 0.10), "cull_disabled": true },
		{ "material_key": "snt:carrot_sprout", "dimension": "overworld",
		  "albedo_color": Color(0.25, 0.45, 0.15), "cull_disabled": true },
		{ "material_key": "snt:carrot_growing", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.55, 0.18), "cull_disabled": true },
		{ "material_key": "snt:carrot_mature", "dimension": "overworld",
		  "albedo_color": Color(0.75, 0.45, 0.15), "cull_disabled": true },

		# Potato stages: green tops, brown tuber.
		{ "material_key": "snt:potato_seed", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.25, 0.10), "cull_disabled": true },
		{ "material_key": "snt:potato_sprout", "dimension": "overworld",
		  "albedo_color": Color(0.28, 0.48, 0.15), "cull_disabled": true },
		{ "material_key": "snt:potato_growing", "dimension": "overworld",
		  "albedo_color": Color(0.32, 0.55, 0.20), "cull_disabled": true },
		{ "material_key": "snt:potato_mature", "dimension": "overworld",
		  "albedo_color": Color(0.55, 0.45, 0.20), "cull_disabled": true },

		# Cotton stages: green→white.
		{ "material_key": "snt:cotton_seed", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.25, 0.10), "cull_disabled": true },
		{ "material_key": "snt:cotton_sprout", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.50, 0.18), "cull_disabled": true },
		{ "material_key": "snt:cotton_growing", "dimension": "overworld",
		  "albedo_color": Color(0.35, 0.55, 0.22), "cull_disabled": true },
		{ "material_key": "snt:cotton_mature", "dimension": "overworld",
		  "albedo_color": Color(0.92, 0.90, 0.85), "cull_disabled": true },

		# Herb stages: green→purple-green.
		{ "material_key": "snt:herb_seed", "dimension": "overworld",
		  "albedo_color": Color(0.25, 0.25, 0.10), "cull_disabled": true },
		{ "material_key": "snt:herb_sprout", "dimension": "overworld",
		  "albedo_color": Color(0.25, 0.45, 0.18), "cull_disabled": true },
		{ "material_key": "snt:herb_growing", "dimension": "overworld",
		  "albedo_color": Color(0.28, 0.50, 0.22), "cull_disabled": true },
		{ "material_key": "snt:herb_mature", "dimension": "overworld",
		  "albedo_color": Color(0.40, 0.55, 0.25), "cull_disabled": true },

		# Pumpkin stages: green→orange.
		{ "material_key": "snt:pumpkin_seed", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.25, 0.10), "cull_disabled": true },
		{ "material_key": "snt:pumpkin_sprout", "dimension": "overworld",
		  "albedo_color": Color(0.25, 0.50, 0.15), "cull_disabled": true },
		{ "material_key": "snt:pumpkin_growing", "dimension": "overworld",
		  "albedo_color": Color(0.30, 0.55, 0.18), "cull_disabled": true },
		{ "material_key": "snt:pumpkin_mature", "dimension": "overworld",
		  "albedo_color": Color(0.90, 0.55, 0.15), "cull_disabled": true },
	]
	for visual in visuals:
		registry.register_material_visual(visual)


static func _register_builtin_material_roles(registry: GDTerrainContentRegistry) -> void:
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


static func _register_tree_species(registry: GDTerrainContentRegistry) -> void:

	# Oak: temperate deciduous, round canopy, most common.
	registry.register_tree_species({
		"species_key": "oak",
		"title_key": "tree.oak",
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

	# Birch: cold-temperate deciduous, tall narrow canopy.
	registry.register_tree_species({
		"species_key": "birch",
		"title_key": "tree.birch",
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

	# Spruce: cold evergreen, cone canopy.
	registry.register_tree_species({
		"species_key": "spruce",
		"title_key": "tree.spruce",
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

	# Acacia: tropical deciduous, umbrella canopy.
	registry.register_tree_species({
		"species_key": "acacia",
		"title_key": "tree.acacia",
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

	# Maple: temperate deciduous, vivid autumn red.
	registry.register_tree_species({
		"species_key": "maple",
		"title_key": "tree.maple",
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

	# Sequoia: warm-temperate evergreen, very tall cone canopy.
	registry.register_tree_species({
		"species_key": "sequoia",
		"title_key": "tree.sequoia",
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

	# Cherry: temperate deciduous, pink blossoms, fruit-bearing.
	registry.register_tree_species({
		"species_key": "cherry",
		"title_key": "tree.cherry",
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

	# Olive: warm-temperate evergreen, fruit-bearing.
	registry.register_tree_species({
		"species_key": "olive",
		"title_key": "tree.olive",
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


static func _register_runtime_material_ids(registry: GDTerrainContentRegistry) -> void:
	registry.set_runtime_material_ids({
		"ladder": "snt:ladder",
		"ladder_key": "snt:ladder",
		"workbench": "snt:workbench",
		"workbench_key": "snt:workbench",
		"farmland": "snt:farmland",
		"fence_key": "snt:fence",
	})


# Register crop species for the Tier 1 planting system.
# Each species defines biome/season constraints, growth ticks, item production,
# and 4 stage material keys. The C++ CropGrowthSystem consumes these definitions
# to advance crop growth stages via scheduled ticks (priority 9).
static func _register_crop_species(registry: GDTerrainContentRegistry) -> void:
	# Wheat: temperate grain, spring plant → summer grow → autumn harvest.
	registry.register_crop_species({
		"species_key": "wheat",
		"title_key": "crop.wheat",
		"category": CROP_GRAIN,
		"temperature_min": -0.3, "temperature_max": 0.6,
		"humidity_min": -0.2, "humidity_max": 0.6,
		"plant_season": SEASON_SPRING,
		"grow_season": SEASON_SUMMER,
		"harvest_season": SEASON_AUTUMN,
		"ticks_seed_to_sprout": 3000,
		"ticks_sprout_to_growing": 6000,
		"ticks_growing_to_mature": 9000,
		"seed_item_key": "seed.wheat",
		"crop_item_key": "crop.wheat",
		"byproduct_item_key": "seed.wheat",
		"crop_min": 1, "crop_max": 2,
		"byproduct_count": 1,
		"repeat_harvest": false,
		"stage_material_keys": [
			"snt:wheat_seed", "snt:wheat_sprout",
			"snt:wheat_growing", "snt:wheat_mature",
		],
		"fertility_sensitivity": 0.7,
		"water_sensitivity": 0.7,
		"wild_spawn": true,
		"wild_density_weight": 1.0,
		"crop_color": Color(0.85, 0.75, 0.25),
	})

	# Carrot: temperate root vegetable.
	registry.register_crop_species({
		"species_key": "carrot",
		"title_key": "crop.carrot",
		"category": CROP_ROOT,
		"temperature_min": -0.2, "temperature_max": 0.7,
		"humidity_min": -0.1, "humidity_max": 0.7,
		"plant_season": SEASON_SPRING,
		"grow_season": SEASON_SUMMER,
		"harvest_season": SEASON_AUTUMN,
		"ticks_seed_to_sprout": 2500,
		"ticks_sprout_to_growing": 5000,
		"ticks_growing_to_mature": 8000,
		"seed_item_key": "seed.carrot",
		"crop_item_key": "crop.carrot",
		"byproduct_item_key": "",
		"crop_min": 1, "crop_max": 3,
		"byproduct_count": 0,
		"repeat_harvest": false,
		"stage_material_keys": [
			"snt:carrot_seed", "snt:carrot_sprout",
			"snt:carrot_growing", "snt:carrot_mature",
		],
		"fertility_sensitivity": 0.5,
		"water_sensitivity": 0.6,
		"wild_spawn": true,
		"wild_density_weight": 0.8,
		"crop_color": Color(0.75, 0.45, 0.15),
	})

	# Potato: hardy root vegetable, wider climate tolerance.
	registry.register_crop_species({
		"species_key": "potato",
		"title_key": "crop.potato",
		"category": CROP_ROOT,
		"temperature_min": -0.4, "temperature_max": 0.5,
		"humidity_min": -0.2, "humidity_max": 0.8,
		"plant_season": SEASON_SPRING,
		"grow_season": SEASON_SUMMER,
		"harvest_season": SEASON_AUTUMN,
		"ticks_seed_to_sprout": 3000,
		"ticks_sprout_to_growing": 5500,
		"ticks_growing_to_mature": 8500,
		"seed_item_key": "seed.potato",
		"crop_item_key": "crop.potato",
		"byproduct_item_key": "seed.potato",
		"crop_min": 1, "crop_max": 3,
		"byproduct_count": 1,
		"repeat_harvest": false,
		"stage_material_keys": [
			"snt:potato_seed", "snt:potato_sprout",
			"snt:potato_growing", "snt:potato_mature",
		],
		"fertility_sensitivity": 0.6,
		"water_sensitivity": 0.5,
		"wild_spawn": false,
		"wild_density_weight": 0.5,
		"crop_color": Color(0.55, 0.45, 0.20),
	})

	# Cotton: warm-temperate fiber crop.
	registry.register_crop_species({
		"species_key": "cotton",
		"title_key": "crop.cotton",
		"category": CROP_FIBER,
		"temperature_min": 0.2, "temperature_max": 0.8,
		"humidity_min": -0.1, "humidity_max": 0.5,
		"plant_season": SEASON_SUMMER,
		"grow_season": SEASON_SUMMER,
		"harvest_season": SEASON_AUTUMN,
		"ticks_seed_to_sprout": 3500,
		"ticks_sprout_to_growing": 7000,
		"ticks_growing_to_mature": 10000,
		"seed_item_key": "seed.cotton",
		"crop_item_key": "crop.cotton",
		"byproduct_item_key": "seed.cotton",
		"crop_min": 1, "crop_max": 2,
		"byproduct_count": 1,
		"repeat_harvest": false,
		"stage_material_keys": [
			"snt:cotton_seed", "snt:cotton_sprout",
			"snt:cotton_growing", "snt:cotton_mature",
		],
		"fertility_sensitivity": 0.8,
		"water_sensitivity": 0.7,
		"wild_spawn": false,
		"wild_density_weight": 0.3,
		"crop_color": Color(0.92, 0.90, 0.85),
	})

	# Herb: medicinal plant, shade-tolerant, any season.
	registry.register_crop_species({
		"species_key": "herb",
		"title_key": "crop.herb",
		"category": CROP_HERB,
		"temperature_min": -0.3, "temperature_max": 0.5,
		"humidity_min": 0.0, "humidity_max": 0.8,
		"plant_season": SEASON_ANY,
		"grow_season": SEASON_ANY,
		"harvest_season": SEASON_ANY,
		"ticks_seed_to_sprout": 2000,
		"ticks_sprout_to_growing": 4000,
		"ticks_growing_to_mature": 6000,
		"seed_item_key": "seed.herb",
		"crop_item_key": "crop.herb",
		"byproduct_item_key": "seed.herb",
		"crop_min": 1, "crop_max": 2,
		"byproduct_count": 1,
		"repeat_harvest": true,
		"regrow_ticks": 5000,
		"stage_material_keys": [
			"snt:herb_seed", "snt:herb_sprout",
			"snt:herb_growing", "snt:herb_mature",
		],
		"fertility_sensitivity": 0.4,
		"water_sensitivity": 0.5,
		"wild_spawn": true,
		"wild_density_weight": 1.2,
		"crop_color": Color(0.40, 0.55, 0.25),
	})

	# Pumpkin: warm-season fruit, larger footprint feel.
	registry.register_crop_species({
		"species_key": "pumpkin",
		"title_key": "crop.pumpkin",
		"category": CROP_FRUIT,
		"temperature_min": 0.3, "temperature_max": 0.9,
		"humidity_min": 0.0, "humidity_max": 0.6,
		"plant_season": SEASON_SUMMER,
		"grow_season": SEASON_SUMMER,
		"harvest_season": SEASON_AUTUMN,
		"ticks_seed_to_sprout": 4000,
		"ticks_sprout_to_growing": 8000,
		"ticks_growing_to_mature": 12000,
		"seed_item_key": "seed.pumpkin",
		"crop_item_key": "crop.pumpkin",
		"byproduct_item_key": "seed.pumpkin",
		"crop_min": 1, "crop_max": 2,
		"byproduct_count": 1,
		"repeat_harvest": false,
		"stage_material_keys": [
			"snt:pumpkin_seed", "snt:pumpkin_sprout",
			"snt:pumpkin_growing", "snt:pumpkin_mature",
		],
		"fertility_sensitivity": 0.7,
		"water_sensitivity": 0.8,
		"wild_spawn": false,
		"wild_density_weight": 0.4,
		"crop_color": Color(0.90, 0.55, 0.15),
	})
