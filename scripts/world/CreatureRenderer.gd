# CreatureRenderer — 3D visual representation of ecosystem proxy creatures.
#
# Listens to GDTickSystem signals for creature spawn/despawn/moved/damaged/killed
# events and updates Node3D positions and visual feedback accordingly.
#
# Each creature is rendered as a simple colored box (Node3D + MeshInstance3D).
# Color and scale are determined by species definition.
# Future: replace boxes with animated 3D models loaded by model_key.
class_name CreatureRenderer
extends Node3D

# --- Configuration ---

@export var tick_system_path: NodePath = ^"../GDTickSystem"

# Duration of the hit flash effect in seconds.
@export var hit_flash_duration: float = 0.15

# --- Internal state ---

var _tick_system: GDTickSystem = null

# creature_id (int) -> Node3D
var _creatures: Dictionary = {}

# creature_id (int) -> species_key (String)
var _creature_species: Dictionary = {}

# creature_id (int) -> flash timer (float)
var _flash_timers: Dictionary = {}

# Pre-built species color table.
# Herbivores: green/blue tones. Predators: red/orange tones.
var _color_table: Dictionary = {
	"glow_deer": Color(0.4, 0.9, 0.6),
	"rock_lizard": Color(0.7, 0.6, 0.35),
	"thunderbird": Color(0.9, 0.85, 0.2),
	"sea_serpent": Color(0.15, 0.5, 0.8),
	"blaze_beast": Color(0.95, 0.35, 0.1),
	"aether_wraith": Color(0.6, 0.3, 0.9),
	"aberrant_ascended": Color(0.5, 0.05, 0.05),
}

# Cached hit flash material (white, emissive).
var _hit_flash_material: StandardMaterial3D = null


func _ready() -> void:
	_tick_system = get_node_or_null(tick_system_path) as GDTickSystem
	_hit_flash_material = _make_hit_flash_material()
	_connect_signals()


func _process(delta: float) -> void:
	_update_flash_timers(delta)


# --- Signal connections ---

func _connect_signals() -> void:
	if _tick_system == null:
		return
	if not _tick_system.creature_spawned.is_connected(_on_creature_spawned):
		_tick_system.creature_spawned.connect(_on_creature_spawned)
	if not _tick_system.creature_despawned.is_connected(_on_creature_despawned):
		_tick_system.creature_despawned.connect(_on_creature_despawned)
	if not _tick_system.creature_moved.is_connected(_on_creature_moved):
		_tick_system.creature_moved.connect(_on_creature_moved)
	if not _tick_system.creature_damaged.is_connected(_on_creature_damaged):
		_tick_system.creature_damaged.connect(_on_creature_damaged)
	if not _tick_system.creature_killed.is_connected(_on_creature_killed):
		_tick_system.creature_killed.connect(_on_creature_killed)


# --- Signal handlers ---

func _on_creature_spawned(creature_id: int, species_key: String,
		_dimension: String, _cx: int, _cy: int, _cz: int) -> void:
	if _creatures.has(creature_id):
		return
	_spawn_creature_node(creature_id, species_key)


func _on_creature_despawned(creature_id: int, _species_key: String,
		_dimension: String, _cx: int, _cy: int, _cz: int) -> void:
	_despawn_creature_node(creature_id)


func _on_creature_moved(creature_id: int, _species_key: String,
		pos_x: float, pos_y: float, pos_z: float) -> void:
	var node := _creatures.get(creature_id) as Node3D
	if node != null:
		node.position = Vector3(pos_x, pos_y, pos_z)


func _on_creature_damaged(creature_id: int, _species_id: int,
		_damage: float, _remaining_health: float,
		_dimension: String, _cx: int, _cy: int, _cz: int) -> void:
	_start_hit_flash(creature_id)


func _on_creature_killed(creature_id: int, _species_id: int,
		_dimension: String, _cx: int, _cy: int, _cz: int) -> void:
	_spawn_kill_particles(creature_id)
	# The creature node will be removed by the creature_despawned signal
	# which fires shortly after creature_killed.


# --- Hit flash effect ---

func _start_hit_flash(creature_id: int) -> void:
	var node := _creatures.get(creature_id) as Node3D
	if node == null:
		return
	var body := node.get_node_or_null("Body") as MeshInstance3D
	if body == null:
		return

	# Store the original material so we can restore it.
	var species_key: String = _creature_species.get(creature_id, "")
	if not body.has_meta("original_material"):
		body.set_meta("original_material", body.material_override)

	# Apply white flash material.
	body.material_override = _hit_flash_material
	_flash_timers[creature_id] = hit_flash_duration


func _update_flash_timers(delta: float) -> void:
	var expired: Array = []
	for creature_id in _flash_timers.keys():
		var timer: float = _flash_timers[creature_id]
		timer -= delta
		if timer <= 0.0:
			expired.append(creature_id)
		else:
			_flash_timers[creature_id] = timer

	for creature_id in expired:
		_flash_timers.erase(creature_id)
		_restore_material(creature_id)


func _restore_material(creature_id: int) -> void:
	var node := _creatures.get(creature_id) as Node3D
	if node == null:
		return
	var body := node.get_node_or_null("Body") as MeshInstance3D
	if body == null:
		return
	if body.has_meta("original_material"):
		body.material_override = body.get_meta("original_material")
		body.remove_meta("original_material")


# --- Kill particle effect ---

func _spawn_kill_particles(creature_id: int) -> void:
	var node := _creatures.get(creature_id) as Node3D
	if node == null:
		return

	var species_key: String = _creature_species.get(creature_id, "")
	var color := _get_species_color(species_key)

	# Create a simple expanding/fading sphere as a death burst.
	var particles := MeshInstance3D.new()
	particles.name = "KillBurst"
	var sphere := SphereMesh.new()
	sphere.radius = 0.1
	sphere.height = 0.2
	sphere.radial_segments = 8
	sphere.rings = 4
	particles.mesh = sphere
	particles.material_override = _make_kill_burst_material(color)
	particles.global_position = node.global_position
	add_child(particles)

	# Animate: scale up and fade out, then free.
	var tween := create_tween()
	tween.tween_property(particles, "scale", Vector3(3.0, 3.0, 3.0), 0.4)
	tween.parallel().tween_property(
		particles.material_override, "albedo_color:a", 0.0, 0.4)
	tween.tween_callback(particles.queue_free)


# --- Creature node management ---

func _spawn_creature_node(creature_id: int, species_key: String) -> void:
	var color := _get_species_color(species_key)
	var scale := 1.0

	var root := Node3D.new()
	root.name = "Creature_%d" % creature_id

	# Body: a small colored box representing the creature.
	var body := MeshInstance3D.new()
	body.name = "Body"
	var mesh := BoxMesh.new()
	mesh.size = Vector3(0.4, 0.4, 0.6) * scale
	body.mesh = mesh
	body.material_override = _make_creature_material(color, species_key)
	body.position = Vector3(0.0, 0.3 * scale, 0.0)
	root.add_child(body)

	# Eye indicator: tiny white dot on front face for direction sense.
	var eye := MeshInstance3D.new()
	eye.name = "Eye"
	var eye_mesh := BoxMesh.new()
	eye_mesh.size = Vector3(0.08, 0.08, 0.02) * scale
	eye.mesh = eye_mesh
	eye.material_override = _make_eye_material()
	eye.position = Vector3(0.0, 0.38 * scale, -0.31 * scale)
	root.add_child(eye)

	add_child(root)
	_creatures[creature_id] = root
	_creature_species[creature_id] = species_key


func _despawn_creature_node(creature_id: int) -> void:
	var node := _creatures.get(creature_id) as Node3D
	if node == null:
		return
	_creatures.erase(creature_id)
	_creature_species.erase(creature_id)
	_flash_timers.erase(creature_id)
	node.queue_free()


# --- Material helpers ---

func _get_species_color(species_key: String) -> Color:
	if _color_table.has(species_key):
		return _color_table[species_key]
	# Default: gray for unknown species.
	return Color(0.6, 0.6, 0.6)


func _make_creature_material(color: Color, species_key: String) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = 0.7
	material.specular_mode = BaseMaterial3D.SPECULAR_DISABLED

	# Predators get slight emission for visibility.
	if species_key == "thunderbird" or species_key == "blaze_beast" \
			or species_key == "aether_wraith":
		material.emission_enabled = true
		material.emission = color * 0.3
		material.emission_energy = 0.5

	return material


func _make_eye_material() -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = Color.WHITE
	material.roughness = 0.3
	material.emission_enabled = true
	material.emission = Color(0.8, 0.8, 0.8)
	material.emission_energy = 0.3
	return material


func _make_hit_flash_material() -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = Color.WHITE
	material.roughness = 0.5
	material.emission_enabled = true
	material.emission = Color(2.0, 2.0, 2.0)
	material.emission_energy = 2.0
	return material


func _make_kill_burst_material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.roughness = 0.5
	material.emission_enabled = true
	material.emission = color * 0.5
	material.emission_energy = 1.0
	return material
