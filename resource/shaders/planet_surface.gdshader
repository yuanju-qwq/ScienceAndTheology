// Planet surface procedural shader for LOD 2/3 proxy spheres.
// Uses 3D noise on the sphere normal direction to produce
// terrain-like color variation: ocean, sand, grass, mountain, snow.
// No external textures required — fully procedural.
shader_type spatial;

render_mode cull_back, depth_draw_opaque, blend_mix;

// Noise seed offset to vary the planet appearance per instance.
uniform vec3 noise_offset = vec3(0.0);
// Sea level threshold (0.0 ~ 1.0). Areas below this are water-colored.
uniform float sea_level = 0.35;
// Snow line threshold (0.0 ~ 1.0). Areas above this are snow-colored.
uniform float snow_line = 0.85;
// Fade alpha for LOD transitions. 1.0 = fully opaque, 0.0 = fully transparent.
uniform float fade_alpha = 1.0;

// Simple 3D value noise (hash-based, no texture lookups).
float hash3d(vec3 p) {
	p = fract(p * vec3(443.897, 441.423, 437.195));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}

float noise3d(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);

	float a = hash3d(i);
	float b = hash3d(i + vec3(1.0, 0.0, 0.0));
	float c = hash3d(i + vec3(0.0, 1.0, 0.0));
	float d = hash3d(i + vec3(1.0, 1.0, 0.0));
	float e = hash3d(i + vec3(0.0, 0.0, 1.0));
	float ff = hash3d(i + vec3(1.0, 0.0, 1.0));
	float g = hash3d(i + vec3(0.0, 1.0, 1.0));
	float h = hash3d(i + vec3(1.0, 1.0, 1.0));

	float x1 = mix(a, b, f.x);
	float x2 = mix(c, d, f.x);
	float x3 = mix(e, ff, f.x);
	float x4 = mix(g, h, f.x);

	float y1 = mix(x1, x2, f.y);
	float y2 = mix(x3, x4, f.y);

	return mix(y1, y2, f.z);
}

// Fractal Brownian Motion — 5 octaves.
float fbm(vec3 p) {
	float value = 0.0;
	float amplitude = 0.5;
	float frequency = 1.0;
	for (int i = 0; i < 5; i++) {
		value += amplitude * noise3d(p * frequency);
		frequency *= 2.0;
		amplitude *= 0.5;
	}
	return value;
}

void fragment() {
	// Use the vertex normal as the direction from planet center.
	// This gives a stable mapping regardless of mesh geometry.
	vec3 dir = normalize(NORMAL);

	// Sample terrain elevation noise.
	float elevation = fbm(dir * 4.0 + noise_offset);

	// Color palette.
	vec3 water_deep = vec3(0.05, 0.15, 0.45);
	vec3 water_shallow = vec3(0.10, 0.35, 0.65);
	vec3 sand = vec3(0.73, 0.64, 0.40);
	vec3 grass = vec3(0.21, 0.42, 0.20);
	vec3 forest = vec3(0.12, 0.30, 0.10);
	vec3 rock = vec3(0.46, 0.47, 0.45);
	vec3 snow = vec3(0.90, 0.92, 0.95);

	vec3 color;
	if (elevation < sea_level - 0.08) {
		// Deep ocean.
		color = mix(water_deep, water_shallow, (elevation) / (sea_level - 0.08));
	} else if (elevation < sea_level) {
		// Shallow water / coast.
		color = mix(water_shallow, sand, (elevation - (sea_level - 0.08)) / 0.08);
	} else if (elevation < sea_level + 0.05) {
		// Beach / sand.
		color = mix(sand, grass, (elevation - sea_level) / 0.05);
	} else if (elevation < 0.55) {
		// Grassland.
		float t = (elevation - (sea_level + 0.05)) / (0.55 - sea_level - 0.05);
		color = mix(grass, forest, t);
	} else if (elevation < snow_line - 0.05) {
		// Mountains / rock.
		float t = (elevation - 0.55) / (snow_line - 0.05 - 0.55);
		color = mix(forest, rock, t);
	} else {
		// Snow caps.
		float t = (elevation - (snow_line - 0.05)) / 0.05;
		color = mix(rock, snow, clamp(t, 0.0, 1.0));
	}

	// Add subtle detail noise for texture variation.
	float detail = noise3d(dir * 20.0 + noise_offset * 3.0) * 0.06;
	color += detail - 0.03;

	ALBEDO = color;
	ROUGHNESS = 0.92;
	METALLIC = 0.0;
	ALPHA = fade_alpha;
}
