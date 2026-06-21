// Terrain block shader with shared texture atlas, per-face tile selection,
// overlay, and position-based variant.
//
// UV   — in block units (0..quad_size); fract(UV) gives the in-tile coordinate
//         so textures tile per-block within a greedy-merged quad.
// UV2.x — face type: 0 = top (+Y), 1 = bottom (-Y), 2 = sides.
// UV2.y — position-based hash [0,1) for random variant selection.
//
// Atlas layout: a grid of atlas_grid.x columns × atlas_grid.y rows.
// Each material-face occupies a base cell (top_tile_base / bottom_tile_base /
// sides_tile_base) and variant_count consecutive cells stacked vertically
// (rows base.y .. base.y + variant_count - 1). The shader picks one variant
// from UV2.y and maps fract(UV) into that cell.
shader_type spatial;

render_mode cull_disabled, depth_prepass_alpha, blend_mix;

// Fallback albedo color when no atlas tile is assigned for the face.
uniform vec4 albedo_color : source_color = vec4(0.85, 0.20, 0.85, 1.0);
uniform float roughness : hint_range(0.0, 1.0) = 0.92;
uniform vec4 emissive_color : source_color = vec4(0.0, 0.0, 0.0, 1.0);

// Shared atlas texture (per-dimension). All materials in a dimension share
// the same atlas; per-material tile offsets select the region to sample.
uniform sampler2D atlas_texture : hint_default_white, filter_nearest, repeat_disable;

// Atlas grid dimensions (cols, rows).
uniform vec2 atlas_grid = vec2(1.0, 1.0);

// Base cell (col, row) for each face group in the atlas grid.
uniform vec2 top_tile_base = vec2(0.0, 0.0);
uniform vec2 bottom_tile_base = vec2(0.0, 0.0);
uniform vec2 sides_tile_base = vec2(0.0, 0.0);

// Number of variant rows stacked vertically from the base cell.
uniform int top_variant_count : hint_range(1, 16) = 1;
uniform int bottom_variant_count : hint_range(1, 16) = 1;
uniform int sides_variant_count : hint_range(1, 16) = 1;

// Flags: whether each face has a valid atlas tile assigned.
uniform bool has_top_texture = false;
uniform bool has_bottom_texture = false;
uniform bool has_sides_texture = false;

// Overlay texture blended on top of the base face texture.
// The overlay uses its own UV (block units) and is NOT part of the atlas;
// it is a standalone repeating texture for ore veins, moss, cracks, etc.
uniform sampler2D overlay_texture : hint_default_white, filter_nearest, repeat_enable;
uniform float overlay_blend : hint_range(0.0, 1.0) = 0.5;
uniform bool has_overlay = false;

// Sample the atlas for a given base cell and variant count.
// variant_hash selects one of variant_count rows; fract(uv) tiles per-block.
vec4 sample_atlas(vec2 uv, float variant_hash, vec2 base, int variant_count) {
	vec2 cell_size = vec2(1.0) / atlas_grid;
	float variant_idx = floor(variant_hash * float(variant_count));
	variant_idx = clamp(variant_idx, 0.0, float(variant_count) - 1.0);
	vec2 tile_offset = vec2(base.x * cell_size.x,
	                        (base.y + variant_idx) * cell_size.y);
	vec2 local_uv = fract(uv);
	vec2 atlas_uv = tile_offset + local_uv * cell_size;
	return texture(atlas_texture, atlas_uv);
}

void fragment() {
	// UV2.x = face type (0=top, 1=bottom, 2=sides).
	// UV2.y = position hash [0,1) for variant selection.
	float face_type = UV2.x;
	float variant_hash = UV2.y;

	vec4 tex_color = vec4(1.0);
	bool has_tex = false;
	vec2 face_uv = UV;

	if (face_type < 0.5) {
		// Top face.
		tex_color = sample_atlas(UV, variant_hash, top_tile_base, top_variant_count);
		has_tex = has_top_texture;
	} else if (face_type < 1.5) {
		// Bottom face.
		tex_color = sample_atlas(UV, variant_hash, bottom_tile_base, bottom_variant_count);
		has_tex = has_bottom_texture;
	} else {
		// Sides face.
		tex_color = sample_atlas(UV, variant_hash, sides_tile_base, sides_variant_count);
		has_tex = has_sides_texture;
	}

	vec3 base_color;
	float base_alpha;

	if (has_tex) {
		base_color = tex_color.rgb * albedo_color.rgb;
		base_alpha = tex_color.a * albedo_color.a;
	} else {
		base_color = albedo_color.rgb;
		base_alpha = albedo_color.a;
	}

	// Apply overlay if present (standalone repeating texture, not atlas).
	if (has_overlay) {
		vec4 overlay_color = texture(overlay_texture, face_uv);
		base_color = mix(base_color, overlay_color.rgb * base_color, overlay_blend * overlay_color.a);
	}

	ALBEDO = base_color;
	ALPHA = base_alpha;
	ROUGHNESS = roughness;
	METALLIC = 0.0;

	if (length(emissive_color.rgb) > 0.001) {
		EMISSION = emissive_color.rgb;
	}
}
