@tool
class_name PrototypeLayerArt
extends Node2D

enum ArtTheme {
	SURFACE,
	UNDERGROUND,
}

@export var texture: Texture2D:
	set(value):
		texture = value
		queue_redraw()

@export var art_theme := ArtTheme.SURFACE:
	set(value):
		art_theme = value
		queue_redraw()

@export var tile_size := 32:
	set(value):
		tile_size = value
		queue_redraw()

@export var origin_cell := Vector2i(-5, -3):
	set(value):
		origin_cell = value
		queue_redraw()

@export var map_size := Vector2i(11, 7):
	set(value):
		map_size = value
		queue_redraw()

@export var tile_source_margin := 0:
	set(value):
		tile_source_margin = value
		queue_redraw()


func _draw() -> void:
	if texture == null:
		return

	match art_theme:
		ArtTheme.SURFACE:
			_draw_surface()
		ArtTheme.UNDERGROUND:
			_draw_underground()


func _draw_surface() -> void:
	for y in range(origin_cell.y, origin_cell.y + map_size.y):
		for x in range(origin_cell.x, origin_cell.x + map_size.x):
			_draw_tile(Vector2i(x, y), _surface_ground_tile(x, y))

	for x in range(origin_cell.x, origin_cell.x + map_size.x):
		_draw_tile(Vector2i(x, 0), Vector2i(4 + abs(x) % 2, 0))


func _draw_underground() -> void:
	for y in range(origin_cell.y, origin_cell.y + map_size.y):
		for x in range(origin_cell.x, origin_cell.x + map_size.x):
			_draw_tile(Vector2i(x, y), _underground_floor_tile(x, y))

	for x in range(origin_cell.x + 1, origin_cell.x + map_size.x - 1):
		_draw_tile(Vector2i(x, 0), Vector2i(3 + abs(x) % 2, 3))

	for y in range(-1, 2):
		for x in range(-2, 2):
			_draw_tile(Vector2i(x, y), Vector2i(6 + abs(x + y) % 3, 3))


func _surface_ground_tile(x: int, y: int) -> Vector2i:
	var variant := absi((x * 7 + y * 3) % 4)
	return Vector2i(variant, 0)


func _underground_floor_tile(x: int, y: int) -> Vector2i:
	var variant := absi((x * 5 + y * 2) % 3)
	return Vector2i(variant, 3)


func _draw_tile(cell_position: Vector2i, atlas_position: Vector2i) -> void:
	var tile_vector := Vector2(tile_size, tile_size)
	var destination := Rect2(
			Vector2(cell_position * tile_size) - tile_vector * 0.5,
			tile_vector)
	var margin := clampi(tile_source_margin, 0, tile_size / 3)
	var source := Rect2(
			Vector2(atlas_position * tile_size) + Vector2(margin, margin),
			tile_vector - Vector2(margin * 2, margin * 2))
	draw_texture_rect_region(texture, destination, source)
