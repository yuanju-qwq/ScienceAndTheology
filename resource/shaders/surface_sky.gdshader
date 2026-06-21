// Atmosphere sky used while standing on a planet. Sun and moon directions
// come from the same day/night state as the directional lights, so the visible
// discs, shadows and horizon transitions stay synchronized.
shader_type sky;

uniform vec3 local_up = vec3(0.0, 1.0, 0.0);
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 moon_direction = vec3(0.0, -1.0, 0.0);
uniform vec3 sun_color : source_color = vec3(1.0, 0.95, 0.82);
uniform vec3 moon_color : source_color = vec3(0.62, 0.70, 0.90);
uniform float day_factor : hint_range(0.0, 1.0) = 1.0;
uniform float sun_visibility : hint_range(0.0, 1.0) = 1.0;
uniform float moon_visibility : hint_range(0.0, 1.0) = 0.0;

float hash3(vec3 p) {
	p = fract(p * 0.1031);
	p += dot(p, p.yzx + 33.33);
	return fract((p.x + p.y) * p.z);
}

void sky() {
	vec3 view_dir = normalize(EYEDIR);
	float elevation = dot(view_dir, normalize(local_up));
	float height = clamp(elevation * 1.4 + 0.12, 0.0, 1.0);

	vec3 night_top = vec3(0.003, 0.006, 0.018);
	vec3 night_horizon = vec3(0.018, 0.025, 0.055);
	vec3 day_top = vec3(0.055, 0.22, 0.58);
	vec3 day_horizon = vec3(0.50, 0.69, 0.92);
	vec3 top = mix(night_top, day_top, day_factor);
	vec3 horizon = mix(night_horizon, day_horizon, day_factor);
	vec3 color = mix(horizon, top, smoothstep(0.0, 0.88, height));

	// Sparse stars fade naturally through twilight.
	float star_cell = hash3(floor(view_dir * 900.0));
	float stars = smoothstep(0.9975, 1.0, star_cell)
		* (1.0 - day_factor) * smoothstep(-0.18, 0.12, elevation);
	color += vec3(stars * 1.8);

	float sun_dot = dot(view_dir, normalize(sun_direction));
	float sun_disc = smoothstep(0.99972, 0.99986, sun_dot) * sun_visibility;
	float sun_glow = pow(max(sun_dot, 0.0), 220.0) * sun_visibility;
	color += sun_color * (sun_disc * 5.0 + sun_glow * 0.65);

	float moon_dot = dot(view_dir, normalize(moon_direction));
	float moon_disc = smoothstep(0.99960, 0.99978, moon_dot) * moon_visibility;
	float moon_shade = smoothstep(-0.25, 0.35,
		dot(normalize(moon_direction), -normalize(sun_direction)));
	color += moon_color * moon_disc * mix(0.65, 1.25, moon_shade);

	COLOR = color;
}
