# BlockAtlasBuilder — builds a shared texture atlas per dimension from the
# material visual definitions registered in WorldGenConfig.
#
# The greedy mesher (C++) already emits UV in block units (0..quad_size) and
# UV2.x = face type, UV2.y = variant hash. The atlas is sampled in the shader
# via fract(UV) mapped into a per-material-face tile cell, so the C++ mesh
# builder does NOT need to change.
#
# Atlas layout: a grid of `grid.x` columns × `grid.y` rows of equal-sized
# cells (tile_size × tile_size). Each unique texture_path is assigned one
# column; variant_count consecutive rows from row 0 hold the variants. The
# same source texture is blitted into each of its variant rows so sampling
# is always correct (multi-file distinct variants are a future extension).
class_name BlockAtlasBuilder
extends RefCounted

const DEFAULT_TILE_SIZE := 32


# Build a shared atlas for one dimension's material visuals.
#
# `visuals` — Array of Dictionary, as returned by
#   WorldGenConfig.get_material_visuals(). Each entry has:
#   material_id, albedo_color, and optional top/bottom/sides sub-Dicts with
#   texture_path + variant_count.
#
# Returns a Dictionary:
#   {
#     "texture": ImageTexture,  # shared atlas (white 1x1 if no textures)
#     "grid": Vector2i,         # (cols, rows) of the atlas grid
#     "tiles": Dictionary,      # material_id -> { top/bottom/sides: { base, variant_count, has } }
#   }
static func build_atlas(visuals: Array, tile_size: int = DEFAULT_TILE_SIZE) -> Dictionary:
	var unique_paths: PackedStringArray = PackedStringArray()
	var path_to_col: Dictionary = {}  # texture_path -> column index
	var path_variant_count: Dictionary = {}  # texture_path -> variant_count

	# Collect unique texture paths and their variant counts.
	for visual: Dictionary in visuals:
		for face_key: String in ["top", "bottom", "sides"]:
			var face: Dictionary = visual.get(face_key, {})
			var path: String = face.get("texture_path", "")
			if path == "":
				continue
			if not path_to_col.has(path):
				path_to_col[path] = unique_paths.size()
				unique_paths.append(path)
			var vcount: int = int(face.get("variant_count", 1))
			vcount = max(1, vcount)
			# Keep the max variant count seen for this path.
			var existing: int = int(path_variant_count.get(path, 1))
			if vcount > existing:
				path_variant_count[path] = vcount

	# Determine grid dimensions.
	var cols: int = max(1, unique_paths.size())
	var max_variants: int = 1
	for path in path_variant_count.keys():
		max_variants = max(max_variants, int(path_variant_count[path]))
	var rows: int = max_variants

	var grid := Vector2i(cols, rows)

	# Build the atlas image.
	var atlas_image: Image
	if unique_paths.is_empty():
		# No textures: return a 1x1 white image so the shader's default_white
		# hint is satisfied without allocating a large atlas.
		atlas_image = Image.create(1, 1, false, Image.FORMAT_RGBA8)
		atlas_image.fill(Color(1, 1, 1, 1))
		return {
			"texture": ImageTexture.create_from_image(atlas_image),
			"grid": Vector2i(1, 1),
			"tiles": {},
		}

	var atlas_w: int = cols * tile_size
	var atlas_h: int = rows * tile_size
	atlas_image = Image.create(atlas_w, atlas_h, false, Image.FORMAT_RGBA8)
	atlas_image.fill(Color(1, 1, 1, 1))

	# Blit each unique texture into its column, once per variant row.
	for path: String in path_to_col.keys():
		var col: int = int(path_to_col[path])
		var vcount: int = int(path_variant_count[path])
		var src_tex: Texture2D = load(path) as Texture2D
		if src_tex == null:
			push_warning("BlockAtlasBuilder: failed to load texture '%s'" % path)
			continue
		var src_img: Image = src_tex.get_image()
		if src_img == null:
			push_warning("BlockAtlasBuilder: no image data for texture '%s'" % path)
			continue
		# Resize source to tile_size × tile_size if needed.
		if src_img.get_width() != tile_size or src_img.get_height() != tile_size:
			src_img = src_img.duplicate()
			src_img.resize(tile_size, tile_size, Image.INTERPOLATE_NEAREST)
		# Blit the same source into each variant row of this column.
		for vi in range(vcount):
			var dst_x: int = col * tile_size
			var dst_y: int = vi * tile_size
			atlas_image.blend_rect(src_img, Rect2i(0, 0, tile_size, tile_size),
					Vector2i(dst_x, dst_y))

	# Build per-material tile lookup.
	var tiles: Dictionary = {}
	for visual: Dictionary in visuals:
		var mid: int = int(visual.get("material_id", -1))
		if mid < 0:
			continue
		var entry: Dictionary = {}
		for face_key: String in ["top", "bottom", "sides"]:
			var face: Dictionary = visual.get(face_key, {})
			var path: String = face.get("texture_path", "")
			if path != "" and path_to_col.has(path):
				entry[face_key] = {
					"base": Vector2i(int(path_to_col[path]), 0),
					"variant_count": int(path_variant_count[path]),
					"has": true,
				}
			else:
				entry[face_key] = {
					"base": Vector2i(0, 0),
					"variant_count": 1,
					"has": false,
				}
		tiles[mid] = entry

	return {
		"texture": ImageTexture.create_from_image(atlas_image),
		"grid": grid,
		"tiles": tiles,
	}
