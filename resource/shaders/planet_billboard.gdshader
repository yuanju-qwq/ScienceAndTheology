// Planet billboard shader for LOD 4 (extreme distance / deep space).
// Renders a planet disc with procedural surface coloring and atmosphere
// glow on a camera-facing quad. Cheaper than any 3D sphere mesh.
//
// The quad should be sized to 2 * planet_radius * atmosphere_scale so
// the atmosphere glow ring fits between the planet disc edge and the
// quad edge. The planet disc radius in UV space is 1.0 / atmosphere_scale.
shader_type spatial;

render_mode cull_back, depth_draw_opaque, blend_mix, unshaded, billboard_enabled;

// Noise seed offset to vary the planet appearance per instance.
uniform vec3 noise_offset = vec3(0.0);
// Sea level threshold (0.0 ~ 1.0). Areas below this are water-colored.
uniform float sea_level = 0.35;
// Snow line threshold (0.0 ~ 1.0). Areas above this are snow-colored.
uniform float snow_line = 0.85;
// Atmosphere base color (matches planet_atmosphere.gdshader).
uniform vec3 atmosphere_color : source_color = vec3(0.3, 0.6, 1.0);
// Overall atmosphere intensity multiplier.
uniform float atmosphere_intensity = 1.2;
// Radius of the planet disc in UV space (1.0 / atmosphere_scale).
// Inside this radius: planet surface. Outside: atmosphere glow ring.
uniform float disc_radius = 0.93;
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
	// UV is in [0, 1]. Map to [-1, 1] centered on the quad.
	vec2 uv = UV * 2.0 - 1.0;
	float dist = length(uv);

	// Outside the atmosphere glow ring: fully transparent.
	if (dist > 1.0) {
		discard;
	}

	// Planet disc radius in UV space.
	float r = disc_radius;

	// --- Atmosphere glow ring (outside the planet disc) ---
	if (dist > r) {
		// Fresnel-like falloff: brightest at the disc edge, fading outward.
		float t = (1.0 - dist) / (1.0 - r);
		t = clamp(t, 0.0, 1.0);
		float glow = pow(t, 2.0);
		vec3 glow_color = atmosphere_color * atmosphere_intensity * glow;
		ALBEDO = glow_color;
		ALPHA = fade_alpha * glow;
		return;
	}

	// --- Planet surface (inside the disc) ---
	// Compute a fake sphere normal from the UV position.
	// This maps the flat quad to a hemisphere facing the camera,
	// giving stable noise sampling and limb darkening.
	float nz = sqrt(max(0.0, 1.0 - dist * dist));
	vec3 normal = normalize(vec3(uv.x, uv.y, nz));

	// Sample terrain elevation noise using the fake sphere normal.
	float elevation = fbm(normal * 4.0 + noise_offset);

	// Color palette (matches planet_surface.gdshader).
	vec3 water_deep = vec3(0.05, 0.15, 0.45);
	vec3 water_shallow = vec3(0.10, 0.35, 0.65);
	vec3 sand = vec3(0.73, 0.64, 0.40);
	vec3 grass = vec3(0.21, 0.42, 0.20);
	vec3 forest = vec3(0.12, 0.30, 0.10);
	vec3 rock = vec3(0.46, 0.47, 0.45);
	vec3 snow = vec3(0.90, 0.92, 0.95);

	vec3 color;
	if (elevation < sea_level - 0.08) {
		color = mix(water_deep, water_shallow, (elevation) / (sea_level - 0.08));
	} else if (elevation < sea_level) {
		color = mix(water_shallow, sand, (elevation - (sea_level - 0.08)) / 0.08);
	} else if (elevation < sea_level + 0.05) {
		color = mix(sand, grass, (elevation - sea_level) / 0.05);
	} else if (elevation < 0.55) {
		float t = (elevation - (sea_level + 0.05)) / (0.55 - sea_level - 0.05);
		color = mix(grass, forest, t);
	} else if (elevation < snow_line - 0.05) {
		float t = (elevation - 0.55) / (snow_line - 0.05 - 0.55);
		color = mix(forest, rock, t);
	} else {
		float t = (elevation - (snow_line - 0.05)) / 0.05;
		color = mix(rock, snow, clamp(t, 0.0, 1.0));
	}

	// Limb darkening: surface curves away at the edges, so less light
	// reaches the viewer. nz is 1.0 at center, 0.0 at the limb.
	float limb = normal.z;
	color *= mix(0.35, 1.0, limb);

	// Inner atmosphere glow near the limb (inside the disc).
	float inner_glow = pow(1.0 - limb, 3.0);
	color += atmosphere_color * atmosphere_intensity * inner_glow * 0.25;

	// Smooth alpha at the disc edge to avoid aliasing.
	float edge_alpha = smoothstep(r, r - 0.02, dist);

	ALBEDO = color;
	ALPHA = fade_alpha * edge_alpha;
}
