// Planet cloud layer shader for the procedural cloud shell.
// Uses 3D FBM noise on the sphere surface to generate cloud patterns.
// The cloud shell is rendered on back faces (like the atmosphere)
// so clouds wrap around the planet from the viewer's perspective.
// A time uniform drives slow rotation for dynamic cloud movement.
shader_type spatial;

render_mode cull_front, blend_mix, depth_draw_never, unshaded;

// Noise seed offset to vary cloud patterns per planet.
uniform vec3 noise_offset = vec3(0.0);
// Cloud coverage threshold (0.0 ~ 1.0). Higher = less cloud coverage.
uniform float cloud_coverage = 0.45;
// Cloud density / sharpness. Higher = sharper cloud edges.
uniform float cloud_sharpness = 2.5;
// Cloud base color.
uniform vec3 cloud_color : source_color = vec3(0.95, 0.95, 0.97);
// Time for cloud rotation animation.
uniform float time = 0.0;
// Rotation speed around the Y axis (radians per second).
uniform float rotation_speed = 0.02;
// Fade alpha for LOD transitions.
uniform float fade_alpha = 1.0;

// Simple 3D value noise (hash-based).
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

// Fractal Brownian Motion — 4 octaves (fewer than terrain for performance).
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

void fragment() {
	// Apply rotation around Y axis using the time uniform.
	float angle = time * rotation_speed;
	float ca = cos(angle);
	float sa = sin(angle);
	vec3 rotated_dir = vec3(
		NORMAL.x * ca - NORMAL.z * sa,
		NORMAL.y,
		NORMAL.x * sa + NORMAL.z * ca
	);
	vec3 dir = normalize(rotated_dir);

	// Sample cloud noise.
	float n = fbm(dir * 6.0 + noise_offset);

	// Remap noise: cloud_coverage controls the threshold.
	// Areas above the threshold become clouds.
	float cloud = smoothstep(cloud_coverage, cloud_coverage + 0.15, n);

	// Apply sharpness to make edges more defined.
	cloud = pow(cloud, 1.0 / cloud_sharpness);

	ALBEDO = cloud_color;
	ALPHA = cloud * 0.75 * fade_alpha;
}
