# CreatureRenderer — 3D visual representation of ecosystem proxy creatures.
#
# Listens to GDTickSystem signals for creature spawn/despawn/moved events
# and updates Node3D positions accordingly.
#
# Each creature is rendered as a simple colored box (Node3D + MeshInstance3D).
# Color and scale are determined by species definition.
# Future: replace boxes with animated 3D models loaded by model_key.
class_name CreatureRenderer
extends Node3D

# --- Configuration ---

@export var tick_system_path: NodePath = ^"../GDTickSystem"

# --- Internal state ---

var _tick_system: GDTickSystem = null

# creature_id (int) -> Node3D
var _creatures: Dictionary = {}

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


func _ready() -> void:
	_tick_system = get_node_or_null(tick_system_path) as GDTickSystem
	_connect_signals()


func _process(_delta: float) -> void:
	pass


# --- Signal connections ---

func _connect_signals() -> void:
	if _tick_system == null:
		return
	if not _tick_system.creature_spawned.is_connected(_on_creature_spawned):
		_tick_system.creature_spawned.connect(_on_creature_spawned)
	if not _tick_system.creature_despawned.is_connected(_on_creature_despawned):
		_tick_system.creature_despawned.connect(_on_creature_despawned)
	if _tick_system.has_signal("creature_moved"):
		if not _tick_system.creature_moved.is_connected(_on_creature_moved):
			_tick_system.creature_moved.connect(_on_creature_moved)


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


func _despawn_creature_node(creature_id: int) -> void:
	var node := _creatures.get(creature_id) as Node3D
	if node == null:
		return
	_creatures.erase(creature_id)
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
