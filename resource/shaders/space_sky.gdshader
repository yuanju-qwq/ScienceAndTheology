// Procedural star field sky shader for outer space.
// Uses layered hash-based noise to place stars of varying brightness
// and size across the full sky sphere. A subtle nebula gradient adds
// depth and color variation to the background.
shader_type sky;

// Number of noise layers for star density. More layers = denser stars.
uniform int star_layers : hint_range(1, 8) = 4;
// Overall brightness multiplier for stars.
uniform float star_brightness = 1.5;
// Seed offset to vary star patterns per universe.
uniform float star_seed = 0.0;
// Nebula color A (warm).
uniform vec3 nebula_color_a : source_color = vec3(0.12, 0.04, 0.18);
// Nebula color B (cool).
uniform vec3 nebula_color_b : source_color = vec3(0.02, 0.06, 0.16);
// Nebula intensity (0.0 = no nebula, 1.0 = full).
uniform float nebula_intensity = 0.3;

// Hash function for star placement.
float hash2d(vec2 p) {
	p = fract(p * vec2(443.897, 441.423));
	p += dot(p, p.yx + 19.19);
	return fract((p.x + p.y) * p.x);
}

// Star layer: returns brightness of a star at the given direction.
float star_layer(vec2 uv, float layer_seed) {
	// Grid-based star placement: each cell may contain one star.
	vec2 cell_size = vec2(0.02, 0.02);
	vec2 cell_id = floor(uv / cell_size);
	vec2 cell_uv = fract(uv / cell_size);

	// Random position within the cell.
	vec2 star_pos = vec2(
		hash2d(cell_id + layer_seed),
		hash2d(cell_id + layer_seed + 100.0)
	);

	// Distance from the current fragment to the star center.
	float dist = length(cell_uv - star_pos);

	// Random brightness for this star.
	float brightness = hash2d(cell_id + layer_seed + 200.0);
	// Only show a fraction of potential stars.
	float threshold = 0.6;
	if (brightness < threshold) {
		return 0.0;
	}

	// Remap brightness: brighter stars are rarer.
	brightness = (brightness - threshold) / (1.0 - threshold);
	brightness = pow(brightness, 2.0);

	// Star size: brighter stars are slightly larger.
	float size = 0.003 + brightness * 0.005;

	// Smooth falloff from star center.
	float star = smoothstep(size, 0.0, dist);

	// Add a soft glow around brighter stars.
	float glow_size = size * 3.0;
	float glow = smoothstep(glow_size, 0.0, dist) * 0.3 * brightness;

	return (star + glow) * brightness;
}

// Simple 3D noise for nebula.
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

float fbm(vec3 p) {
	float value = 0.0;
	float amplitude = 0.5;
	float frequency = 1.0;
	for (int i = 0; i < 4; i++) {
		value += amplitude * noise3d(p * frequency);
		frequency *= 2.0;
		amplitude *= 0.5;
	}
	return value;
}

void sky() {
	// Use the ray direction to sample stars on a sphere.
	vec3 dir = EYEDIR;
	// Convert 3D direction to 2D UV for star grid sampling.
	// Use equirectangular projection.
	vec2 uv = vec2(atan(dir.z, dir.x), asin(clamp(dir.y, -1.0, 1.0)));
	uv = uv / 6.28318530718 + 0.5;

	// Accumulate star layers with different seeds for density.
	float stars = 0.0;
	for (int i = 0; i < star_layers; i++) {
		float layer_seed = star_seed + float(i) * 137.0;
		stars += star_layer(uv, layer_seed) / (1.0 + float(i) * 0.5);
	}
	stars *= star_brightness;

	// Subtle nebula using FBM noise on the direction.
	float n = fbm(dir * 2.0 + star_seed);
	vec3 nebula = mix(nebula_color_a, nebula_color_b, n);
	nebula *= smoothstep(0.3, 0.7, n) * nebula_intensity;

	// Combine: deep space background + nebula + stars.
	vec3 background = vec3(0.005, 0.005, 0.012);
	vec3 color = background + nebula + vec3(stars);

	COLOR = color;
}
