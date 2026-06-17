// Planet atmosphere shader for the outer glow shell.
// Uses a Fresnel effect to simulate atmospheric scattering:
// the atmosphere is most visible at grazing angles (limb brightening)
// and nearly transparent when looking straight down at the surface.
// Rendered on back faces of a slightly larger sphere so the
// atmosphere wraps around the planet from the viewer's perspective.
shader_type spatial;

render_mode cull_front, blend_add, depth_draw_never, unshaded;

// Atmosphere base color (e.g., blue for Earth-like).
uniform vec3 atmosphere_color : source_color = vec3(0.3, 0.6, 1.0);
// Controls how quickly the atmosphere falls off from edge to center.
// Higher values = thinner atmosphere band at the limb.
uniform float atmosphere_power = 3.5;
// Overall intensity multiplier.
uniform float atmosphere_intensity = 1.2;
// Fade alpha for LOD transitions.
uniform float fade_alpha = 1.0;

void fragment() {
	// Fresnel: dot between view direction and surface normal.
	// At the edge (grazing angle), dot ~ 0 → atmosphere is bright.
	// At the center (straight on), dot ~ 1 → atmosphere is transparent.
	float fresnel = 1.0 - dot(VIEW, NORMAL);
	fresnel = clamp(fresnel, 0.0, 1.0);
	fresnel = pow(fresnel, atmosphere_power);

	vec3 color = atmosphere_color * fresnel * atmosphere_intensity;
	ALBEDO = color;
	ALPHA = fresnel * fade_alpha;
}
