// Transparent terrain material for water and ice. It mirrors the opaque
// atlas shader but writes depth so overlapping chunk surfaces cannot build up
// into the large translucent walls seen with ordinary alpha sorting.
shader_type spatial;

render_mode cull_back, depth_draw_always, blend_mix;

uniform vec4 albedo_color : source_color = vec4(0.18, 0.39, 0.74, 0.78);
uniform float roughness : hint_range(0.0, 1.0) = 0.1;
uniform vec4 emissive_color : source_color = vec4(0.0, 0.0, 0.0, 1.0);

uniform sampler2D atlas_texture : hint_default_white, filter_nearest, repeat_disable;
uniform vec2 atlas_grid = vec2(1.0, 1.0);

uniform vec2 top_tile_base = vec2(0.0, 0.0);
uniform vec2 bottom_tile_base = vec2(0.0, 0.0);
uniform vec2 sides_tile_base = vec2(0.0, 0.0);

uniform int top_variant_count : hint_range(1, 16) = 1;
uniform int bottom_variant_count : hint_range(1, 16) = 1;
uniform int sides_variant_count : hint_range(1, 16) = 1;

uniform bool has_top_texture = false;
uniform bool has_bottom_texture = false;
uniform bool has_sides_texture = false;

uniform sampler2D overlay_texture : hint_default_white, filter_nearest, repeat_enable;
uniform float overlay_blend : hint_range(0.0, 1.0) = 0.5;
uniform bool has_overlay = false;

vec4 sample_atlas(vec2 uv, float variant_hash, vec2 base, int variant_count) {
	vec2 cell_size = vec2(1.0) / atlas_grid;
	float variant_idx = floor(variant_hash * float(variant_count));
	variant_idx = clamp(variant_idx, 0.0, float(variant_count) - 1.0);
	vec2 tile_offset = vec2(base.x * cell_size.x,
		(base.y + variant_idx) * cell_size.y);
	return texture(atlas_texture, tile_offset + fract(uv) * cell_size);
}

void fragment() {
	float face_type = UV2.x;
	float variant_hash = UV2.y;
	vec4 tex_color = vec4(1.0);
	bool has_tex = false;

	if (face_type < 0.5) {
		tex_color = sample_atlas(UV, variant_hash, top_tile_base, top_variant_count);
		has_tex = has_top_texture;
	} else if (face_type < 1.5) {
		tex_color = sample_atlas(UV, variant_hash, bottom_tile_base, bottom_variant_count);
		has_tex = has_bottom_texture;
	} else {
		tex_color = sample_atlas(UV, variant_hash, sides_tile_base, sides_variant_count);
		has_tex = has_sides_texture;
	}

	vec3 base_color = albedo_color.rgb;
	float base_alpha = albedo_color.a;
	if (has_tex) {
		base_color = tex_color.rgb * albedo_color.rgb;
		base_alpha = tex_color.a * albedo_color.a;
	}

	if (has_overlay) {
		vec4 overlay_color = texture(overlay_texture, UV);
		base_color = mix(base_color, overlay_color.rgb * base_color,
			overlay_blend * overlay_color.a);
	}

	ALBEDO = base_color;
	ALPHA = base_alpha;
	ROUGHNESS = roughness;
	METALLIC = 0.0;
	if (length(emissive_color.rgb) > 0.001) {
		EMISSION = emissive_color.rgb;
	}
}
