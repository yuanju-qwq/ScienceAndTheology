@tool
class_name TileSetPipeline
extends Node

## Configures a TileSet resource with physics, navigation, and terrain sets
## for the dual-layer terrain tileset.
##
## Usage:
##   1. Open this scene in the Godot editor.
##   2. Assign your TileSet resource to `target_tileset`.
##   3. Click "Configure" in the inspector (or call configure()).
##   4. Save the TileSet resource.

signal configuration_applied

enum TerrainType {
	DIRT = 0,
	SAND = 1,
	STONE = 2,
	WATER = 3,
	LAVA = 4,
	CAVE_FLOOR = 5,
}

@export var target_tileset: TileSet = null

@export_category("Physics Layers")
@export var enable_terrain_physics := true
@export var enable_liquid_physics := true

@export_category("Navigation Layers")
@export var enable_walkable_navigation := true

@export_category("Terrain Sets")
@export var enable_terrain_sets := true

@export_category("Tile Atlas")
@export var atlas_source_id: int = 0
@export var tile_size: int = 32


func _validate_property(property: Dictionary) -> void:
	match property.name:
		"enable_terrain_physics", "enable_liquid_physics":
			property.usage |= PROPERTY_USAGE_SCRIPT_VARIABLE
		"enable_walkable_navigation":
			property.usage |= PROPERTY_USAGE_SCRIPT_VARIABLE
		"enable_terrain_sets":
			property.usage |= PROPERTY_USAGE_SCRIPT_VARIABLE


func configure() -> void:
	if target_tileset == null:
		push_error("TileSetPipeline: target_tileset is null")
		return

	_clear_existing_config()
	_setup_physics_layers()
	_setup_navigation_layers()
	_setup_terrain_sets()
	_assign_tile_properties()

	configuration_applied.emit()
	print("TileSetPipeline: configuration applied successfully")


func _clear_existing_config() -> void:
	while target_tileset.get_physics_layers_count() > 0:
		target_tileset.remove_physics_layer(0)
	while target_tileset.get_navigation_layers_count() > 0:
		target_tileset.remove_navigation_layer(0)
	while target_tileset.get_terrain_sets_count() > 0:
		target_tileset.remove_terrain_set(target_tileset.get_terrain_sets_count() - 1)


func _setup_physics_layers() -> void:
	if not enable_terrain_physics and not enable_liquid_physics:
		return

	var idx := 0
	if enable_terrain_physics:
		target_tileset.add_physics_layer()
		target_tileset.set_physics_layer_name(idx, "terrain")
		target_tileset.set_physics_layer_collision_layer(idx, 1)
		idx += 1

	if enable_liquid_physics:
		target_tileset.add_physics_layer()
		target_tileset.set_physics_layer_name(idx, "liquid")
		target_tileset.set_physics_layer_collision_layer(idx, 2)


func _setup_navigation_layers() -> void:
	if not enable_walkable_navigation:
		return

	target_tileset.add_navigation_layer()
	target_tileset.set_navigation_layer_name(0, "walkable")
	target_tileset.set_navigation_layer_layers(0, 1)


func _setup_terrain_sets() -> void:
	if not enable_terrain_sets:
		return

	var terrains := [
		{ "name": "Dirt", "color": Color(0.45, 0.28, 0.14) },
		{ "name": "Sand", "color": Color(0.76, 0.7, 0.5) },
		{ "name": "Stone", "color": Color(0.4, 0.4, 0.42) },
		{ "name": "Water", "color": Color(0.2, 0.4, 0.8) },
		{ "name": "Lava", "color": Color(0.8, 0.3, 0.1) },
		{ "name": "CaveFloor", "color": Color(0.3, 0.25, 0.2) },
	]

	for t in terrains:
		var idx := target_tileset.get_terrain_sets_count()
		target_tileset.add_terrain_set()
		target_tileset.set_terrain_set_name(idx, t.name)
		target_tileset.set_terrain_set_color(idx, t.color)


func _assign_tile_properties() -> void:
	var source := target_tileset.get_source(atlas_source_id) as TileSetAtlasSource
	if source == null:
		push_error("TileSetPipeline: atlas source %d not found" % atlas_source_id)
		return

	# Define tile categories.
	var terrain_tiles := _get_terrain_tile_map()

	var tile_size_f := float(tile_size)
	var full_rect := PackedVector2Array([
		Vector2(0, 0),
		Vector2(tile_size_f, 0),
		Vector2(tile_size_f, tile_size_f),
		Vector2(0, tile_size_f),
	])
	var nav_rect := PackedVector2Array([
		Vector2(0, 0),
		Vector2(tile_size_f, 0),
		Vector2(tile_size_f, tile_size_f),
		Vector2(0, tile_size_f),
	])

	for atlas_coords in source.get_tiles_count():
		var coords := source.get_tile_id(atlas_coords)
		var key := Vector2i(coords.x, coords.y)
		var info := terrain_tiles.get(key, {})

		var has_solid_physics := info.get("physics", false)
		var has_liquid_physics := info.get("liquid", false)
		var has_nav := info.get("nav", false)
		var terrain := info.get("terrain", -1)

		# Terrain peering.
		if terrain >= 0 and enable_terrain_sets:
			source.set_terrain_peering_terrain(coords, terrain)

		# Physics: full rectangle collision.
		if has_solid_physics and enable_terrain_physics:
			var poly_idx := source.add_physics_layer_polygon(coords, 0)
			if poly_idx >= 0:
				source.set_physics_layer_polygon_shape(coords, 0, 0, full_rect)

		if has_liquid_physics and enable_liquid_physics:
			var liq_layer := 1 if enable_terrain_physics else 0
			var poly_idx := source.add_physics_layer_polygon(coords, liq_layer)
			if poly_idx >= 0:
				source.set_physics_layer_polygon_shape(coords, liq_layer, 0, full_rect)

		# Navigation.
		if has_nav and enable_walkable_navigation:
			var nav_poly_idx := source.add_navigation_layer_polygon(coords, 0)
			if nav_poly_idx >= 0:
				source.set_navigation_layer_polygon_shape(coords, 0, 0, nav_rect)


func _get_terrain_tile_map() -> Dictionary:
	# Maps atlas coords -> { physics, liquid, nav, terrain }.
	# Surface terrain tiles.
	var m := {}
	var dirt := TerrainType.DIRT
	var sand := TerrainType.SAND
	var stone := TerrainType.STONE
	var water := TerrainType.WATER
	var lava := TerrainType.LAVA

	# Row 0: Surface ground.
	for x in range(4):
		m[Vector2i(x, 0)] = { "physics": true, "nav": true, "terrain": dirt }

	m[Vector2i(4, 0)] = { "physics": true, "nav": false, "terrain": stone }
	m[Vector2i(5, 0)] = { "physics": true, "nav": false, "terrain": stone }
	m[Vector2i(6, 0)] = { "physics": true, "nav": false, "terrain": stone }
	m[Vector2i(7, 0)] = { "physics": true, "nav": false, "terrain": stone }
	m[Vector2i(8, 0)] = { "physics": false, "liquid": true, "nav": false, "terrain": water }
	m[Vector2i(9, 0)] = { "physics": false, "liquid": true, "nav": false, "terrain": lava }
	m[Vector2i(10, 0)] = { "physics": true, "nav": true, "terrain": dirt }
	m[Vector2i(11, 0)] = { "physics": true, "nav": true, "terrain": dirt }

	# Row 1: Sand surface variants.
	for x in range(12):
		m[Vector2i(x, 1)] = { "physics": true, "nav": true, "terrain": sand }

	# Row 2: Surface decorations (sparse).
	m[Vector2i(1, 2)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(2, 2)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(4, 2)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(5, 2)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(6, 2)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(7, 2)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(8, 2)] = { "physics": false, "nav": true, "terrain": -1 }

	# Row 3: Underground floor.
	var cave := TerrainType.CAVE_FLOOR
	for x in range(3):
		m[Vector2i(x, 3)] = { "physics": true, "nav": true, "terrain": cave }

	m[Vector2i(3, 3)] = { "physics": true, "nav": false, "terrain": stone }
	m[Vector2i(4, 3)] = { "physics": true, "nav": false, "terrain": stone }
	m[Vector2i(5, 3)] = { "physics": true, "nav": false, "terrain": stone }
	m[Vector2i(6, 3)] = { "physics": true, "nav": false, "terrain": stone }
	m[Vector2i(7, 3)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(8, 3)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(9, 3)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(10, 3)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(11, 3)] = { "physics": false, "nav": true, "terrain": -1 }

	# Row 4: Underground misc.
	for x in range(5):
		m[Vector2i(x, 4)] = { "physics": true, "nav": true, "terrain": cave }
	m[Vector2i(5, 4)] = { "physics": true, "nav": true, "terrain": cave }
	m[Vector2i(6, 4)] = { "physics": true, "nav": true, "terrain": sand }
	m[Vector2i(7, 4)] = { "physics": true, "nav": true, "terrain": sand }
	m[Vector2i(8, 4)] = { "physics": true, "nav": true, "terrain": sand }
	m[Vector2i(9, 4)] = { "physics": true, "nav": true, "terrain": sand }
	m[Vector2i(10, 4)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(11, 4)] = { "physics": false, "nav": true, "terrain": -1 }

	# Row 5: Underground misc.
	for x in range(6):
		m[Vector2i(x, 5)] = { "physics": true, "nav": true, "terrain": cave }

	# Row 6: Underground misc / rifts.
	for x in range(4):
		m[Vector2i(x, 6)] = { "physics": true, "nav": true, "terrain": cave }
	m[Vector2i(7, 6)] = { "physics": false, "nav": true, "terrain": -1 }
	m[Vector2i(8, 6)] = { "physics": false, "nav": true, "terrain": -1 }

	return m
