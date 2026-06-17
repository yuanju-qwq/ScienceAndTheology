// Terrain block shader with per-face texture selection, overlay, and variant.
// UV2.x encodes the face type: 0 = top (+Y), 1 = bottom (-Y), 2 = sides.
// UV2.y encodes a position-based hash [0,1) for random texture variant.
// variant_count: number of vertical tiles in the texture atlas.
// The shader offsets UV.y by floor(hash * variant_count) / variant_count.
shader_type spatial;

render_mode cull_disabled, depth_draw_alpha_opaque, blend_mix;

// Fallback albedo color when no texture is assigned.
uniform vec4 albedo_color : source_color = vec4(0.85, 0.20, 0.85, 1.0);
uniform float roughness : hint_range(0.0, 1.0) = 0.92;
uniform vec4 emissive_color : source_color = vec4(0.0, 0.0, 0.0, 1.0);

// Per-face textures. Each may contain multiple variants stacked vertically.
// If variant_count > 1, the texture is treated as a vertical atlas:
// variant_count rows, each row is one variant tile.
uniform sampler2D top_texture : hint_default_white, filter_nearest, repeat_enable;
uniform sampler2D bottom_texture : hint_default_white, filter_nearest, repeat_enable;
uniform sampler2D sides_texture : hint_default_white, filter_nearest, repeat_enable;

// Number of variants per face texture (vertical atlas rows).
uniform int top_variant_count : hint_range(1, 16) = 1;
uniform int bottom_variant_count : hint_range(1, 16) = 1;
uniform int sides_variant_count : hint_range(1, 16) = 1;

// Flags: whether each face has a valid texture assigned.
uniform bool has_top_texture = false;
uniform bool has_bottom_texture = false;
uniform bool has_sides_texture = false;

// Overlay texture blended on top of the base face texture.
uniform sampler2D overlay_texture : hint_default_white, filter_nearest, repeat_enable;
uniform float overlay_blend : hint_range(0.0, 1.0) = 0.5;
uniform bool has_overlay = false;

// Select variant from vertical atlas based on hash and variant count.
vec2 apply_variant(vec2 uv, float hash_val, int count) {
	if (count <= 1) {
		return uv;
	}
	float variant_idx = floor(hash_val * float(count));
	float offset = variant_idx / float(count);
	// Scale UV.y into one atlas row and shift by variant offset.
	return vec2(uv.x, uv.y / float(count) + offset);
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
		face_uv = apply_variant(UV, variant_hash, top_variant_count);
		tex_color = texture(top_texture, face_uv);
		has_tex = has_top_texture;
	} else if (face_type < 1.5) {
		// Bottom face.
		face_uv = apply_variant(UV, variant_hash, bottom_variant_count);
		tex_color = texture(bottom_texture, face_uv);
		has_tex = has_bottom_texture;
	} else {
		// Sides face.
		face_uv = apply_variant(UV, variant_hash, sides_variant_count);
		tex_color = texture(sides_texture, face_uv);
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

	// Apply overlay if present.
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
