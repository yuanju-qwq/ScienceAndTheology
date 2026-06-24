# FlowEditor — visual node-based flow program editor for SFM.
#
# Provides a GraphEdit canvas where players build flow programs by
# placing nodes (triggers, I/O, filters, control flow, math) and
# connecting their ports. Syncs all changes to a GDFlowManager
# instance which drives the C++ simulation engine.
#
# Usage:
#   var editor := FlowEditor.new()
#   editor.set_flow_manager(my_gd_flow_manager)
#   add_child(editor)
extends Control
class_name FlowEditor

const FlowGraphNodeClass = preload("res://scripts/sfm/FlowGraphNode.gd")

## Emitted when the program is modified (for autosave / dirty tracking).
signal program_modified()

var flow_manager: GDFlowManager = null
var graph_edit: GraphEdit = null
var palette: ItemList = null
var container_list: ItemList = null
var toolbar: HBoxContainer = null
var status_label: Label = null

# node_id → FlowGraphNode
var _nodes: Dictionary = {}
# (from_node, from_port, to_node, to_port) → connection_id
var _connections: Dictionary = {}

# Node type categories for the palette.
const PALETTE_CATEGORIES := {
	"Triggers": [
		GDFlowManager.NODE_TRIGGER_TIMER,
		GDFlowManager.NODE_TRIGGER_REDSTONE,
		GDFlowManager.NODE_TRIGGER_ITEM,
	],
	"Item I/O": [
		GDFlowManager.NODE_ITEM_INPUT,
		GDFlowManager.NODE_ITEM_OUTPUT,
		GDFlowManager.NODE_ITEM_FILTER,
	],
	"Fluid I/O": [
		GDFlowManager.NODE_FLUID_INPUT,
		GDFlowManager.NODE_FLUID_OUTPUT,
		GDFlowManager.NODE_FLUID_FILTER,
	],
	"Energy": [
		GDFlowManager.NODE_ENERGY_INPUT,
		GDFlowManager.NODE_ENERGY_OUTPUT,
	],
	"Redstone": [
		GDFlowManager.NODE_REDSTONE_INPUT,
		GDFlowManager.NODE_REDSTONE_OUTPUT,
	],
	"Control Flow": [
		GDFlowManager.NODE_CONDITION,
		GDFlowManager.NODE_LOOP,
		GDFlowManager.NODE_GROUP_INPUT,
		GDFlowManager.NODE_GROUP_OUTPUT,
	],
	"Data": [
		GDFlowManager.NODE_VARIABLE_GET,
		GDFlowManager.NODE_VARIABLE_SET,
		GDFlowManager.NODE_MATH,
		GDFlowManager.NODE_TEXT_LABEL,
		GDFlowManager.NODE_COUNT,
	],
}


func _ready() -> void:
	_build_ui()
	_populate_palette()


## Connect this editor to a GDFlowManager and load its current program.
func set_flow_manager(fm: GDFlowManager) -> void:
	flow_manager = fm
	refresh_from_manager()


## Rebuild the entire graph from the flow manager's current state.
func refresh_from_manager() -> void:
	if flow_manager == null:
		return

	# Clear existing UI.
	_clear_graph()

	# Load nodes.
	var nodes: Array = flow_manager.get_all_nodes()
	for node_info in nodes:
		_create_node_widget(node_info)

	# Load connections.
	var conns: Array = flow_manager.get_all_connections()
	for conn_info in conns:
		_create_connection_visual(conn_info)

	# Update container list.
	_refresh_container_list()


# ============================================================
# UI construction
# ============================================================

func _build_ui() -> void:
	# Main vertical layout.
	var vbox := VBoxContainer.new()
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(vbox)

	# Toolbar.
	toolbar = HBoxContainer.new()
	toolbar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(toolbar)
	_build_toolbar()

	# Main content area: palette | graph | containers.
	var hbox := HBoxContainer.new()
	hbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(hbox)

	# Left: node palette.
	var palette_panel := PanelContainer.new()
	palette_panel.custom_minimum_size.x = 200.0
	hbox.add_child(palette_panel)
	var palette_vbox := VBoxContainer.new()
	palette_panel.add_child(palette_vbox)
	var palette_label := Label.new()
	palette_label.text = "Node Palette"
	palette_vbox.add_child(palette_label)
	palette = ItemList.new()
	palette.size_flags_vertical = Control.SIZE_EXPAND_FILL
	palette.item_activated.connect(_on_palette_item_activated)
	palette_vbox.add_child(palette)

	# Center: graph edit.
	graph_edit = GraphEdit.new()
	graph_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	graph_edit.size_flags_vertical = Control.SIZE_EXPAND_FILL
	graph_edit.connection_request.connect(_on_connection_request)
	graph_edit.disconnection_request.connect(_on_disconnection_request)
	graph_edit.delete_nodes_request.connect(_on_delete_nodes_request)
	hbox.add_child(graph_edit)

	# Right: container list.
	var container_panel := PanelContainer.new()
	container_panel.custom_minimum_size.x = 200.0
	hbox.add_child(container_panel)
	var container_vbox := VBoxContainer.new()
	container_panel.add_child(container_vbox)
	var container_label := Label.new()
	container_label.text = "Containers"
	container_vbox.add_child(container_label)
	container_list = ItemList.new()
	container_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	container_vbox.add_child(container_list)

	# Status bar.
	status_label = Label.new()
	status_label.text = "Ready"
	vbox.add_child(status_label)


func _build_toolbar() -> void:
	var btn_add := Button.new()
	btn_add.text = "Add Node"
	btn_add.pressed.connect(_on_add_node_pressed)
	toolbar.add_child(btn_add)

	var btn_delete := Button.new()
	btn_delete.text = "Delete Selected"
	btn_delete.pressed.connect(_on_delete_selected_pressed)
	toolbar.add_child(btn_delete)

	var btn_save := Button.new()
	btn_save.text = "Save"
	btn_save.pressed.connect(_on_save_pressed)
	toolbar.add_child(btn_save)

	var btn_load := Button.new()
	btn_load.text = "Load"
	btn_load.pressed.connect(_on_load_pressed)
	toolbar.add_child(btn_load)

	var btn_refresh := Button.new()
	btn_refresh.text = "Refresh Containers"
	btn_refresh.pressed.connect(_on_refresh_containers_pressed)
	toolbar.add_child(btn_refresh)

	var btn_clear := Button.new()
	btn_clear.text = "Clear All"
	btn_clear.pressed.connect(_on_clear_pressed)
	toolbar.add_child(btn_clear)


func _populate_palette() -> void:
	palette.clear()
	for category in PALETTE_CATEGORIES.keys():
		palette.add_item("--- " + category + " ---")
		palette.set_item_disabled(palette.item_count - 1, true)
		for node_type in PALETTE_CATEGORIES[category]:
			var name: String = GDFlowManager.get_node_type_name(node_type)
			palette.add_item(name)
			# Store the node type as metadata.
			palette.set_item_metadata(palette.item_count - 1, node_type)


# ============================================================
# Node management
# ============================================================

func _on_palette_item_activated(index: int) -> void:
	if palette.is_item_disabled(index):
		return
	var node_type: int = palette.get_item_metadata(index)
	if node_type == null:
		return
	_add_node_of_type(node_type)


func _on_add_node_pressed() -> void:
	# Add a timer trigger by default (most common starting point).
	_add_node_of_type(GDFlowManager.NODE_TRIGGER_TIMER)


func _add_node_of_type(node_type: int) -> void:
	if flow_manager == null:
		return
	var node_id: int = flow_manager.add_node(node_type)
	var info: Dictionary = flow_manager.get_node_info(node_id)
	# Offset new nodes from the graph center for visibility.
	var center := graph_edit.scroll_offset + graph_edit.size * 0.5
	info["x"] = center.x
	info["y"] = center.y
	flow_manager.set_node_position(node_id, center.x, center.y)
	_create_node_widget(info)
	program_modified.emit()
	status_label.text = "Added node: " + GDFlowManager.get_node_type_name(node_type)


func _create_node_widget(info: Dictionary) -> void:
	var node := FlowGraphNodeClass.new()
	node.setup_from_info(info)
	node.param_changed.connect(_on_node_param_changed)
	node.position_changed.connect(_on_node_position_changed)
	graph_edit.add_child(node)
	_nodes[int(info.get("id", 0))] = node


func _on_node_param_changed(node_id: int, key: String, value: String) -> void:
	if flow_manager == null:
		return
	flow_manager.set_node_param(node_id, key, value)
	program_modified.emit()


func _on_node_position_changed(node_id: int, x: float, y: float) -> void:
	if flow_manager == null:
		return
	flow_manager.set_node_position(node_id, x, y)
	program_modified.emit()


func _on_delete_nodes_request(nodes: Array) -> void:
	for node_name in nodes:
		var node = graph_edit.get_node(NodePath(node_name))
		if node is FlowGraphNode:
			_delete_node(node.node_id)


func _on_delete_selected_pressed() -> void:
	for node in graph_edit.get_children():
		if node is FlowGraphNode and node.selected:
			_delete_node(node.node_id)


func _delete_node(node_id: int) -> void:
	if flow_manager == null:
		return
	flow_manager.remove_node(node_id)
	var node = _nodes.get(node_id)
	if node != null:
		node.queue_free()
		_nodes.erase(node_id)
	# Remove connection entries for this node.
	var keys_to_remove: Array = []
	for key in _connections.keys():
		var parts = key.split("|")
		if int(parts[0]) == node_id or int(parts[2]) == node_id:
			keys_to_remove.append(key)
	for key in keys_to_remove:
		_connections.erase(key)
	program_modified.emit()


# ============================================================
# Connection management
# ============================================================

func _on_connection_request(from_node: StringName, from_port: int,
							to_node: StringName, to_port: int) -> void:
	if flow_manager == null:
		return
	var from_id := _get_node_id_by_name(from_node)
	var to_id := _get_node_id_by_name(to_node)
	if from_id == 0 or to_id == 0:
		return
	var conn_id: int = flow_manager.connect_nodes(from_id, from_port, to_id, to_port)
	if conn_id == 0:
		status_label.text = "Connection rejected (type mismatch or invalid)"
		return
	graph_edit.connect_node(from_node, from_port, to_node, to_port)
	var key := "%d|%d|%d|%d" % [from_id, from_port, to_id, to_port]
	_connections[key] = conn_id
	program_modified.emit()
	status_label.text = "Connected: port %d → port %d" % [from_port, to_port]


func _on_disconnection_request(from_node: StringName, from_port: int,
							   to_node: StringName, to_port: int) -> void:
	if flow_manager == null:
		return
	var from_id := _get_node_id_by_name(from_node)
	var to_id := _get_node_id_by_name(to_node)
	var key := "%d|%d|%d|%d" % [from_id, from_port, to_id, to_port]
	var conn_id: int = _connections.get(key, 0)
	if conn_id == 0:
		return
	flow_manager.disconnect(conn_id)
	graph_edit.disconnect_node(from_node, from_port, to_node, to_port)
	_connections.erase(key)
	program_modified.emit()


func _create_connection_visual(conn_info: Dictionary) -> void:
	var from_id: int = int(conn_info.get("from_node", 0))
	var from_port: int = int(conn_info.get("from_port", 0))
	var to_id: int = int(conn_info.get("to_node", 0))
	var to_port: int = int(conn_info.get("to_port", 0))
	var conn_id: int = int(conn_info.get("id", 0))

	var from_node = _nodes.get(from_id)
	var to_node = _nodes.get(to_id)
	if from_node == null or to_node == null:
		return

	graph_edit.connect_node(from_node.name, from_port, to_node.name, to_port)
	var key := "%d|%d|%d|%d" % [from_id, from_port, to_id, to_port]
	_connections[key] = conn_id


func _get_node_id_by_name(node_name: StringName) -> int:
	for node_id in _nodes.keys():
		if _nodes[node_id].name == node_name:
			return node_id
	return 0


# ============================================================
# Save / Load
# ============================================================

func _on_save_pressed() -> void:
	if flow_manager == null:
		return
	var data: String = flow_manager.serialize()
	# In a full implementation, this would write to disk or a BlockEntity.
	status_label.text = "Program serialized (%d bytes)" % data.length()
	program_modified.emit()


func _on_load_pressed() -> void:
	if flow_manager == null:
		return
	# In a full implementation, this would read from disk or a BlockEntity.
	# For now, just refresh from the manager.
	refresh_from_manager()
	status_label.text = "Program loaded"


# ============================================================
# Container management
# ============================================================

func _on_refresh_containers_pressed() -> void:
	_refresh_container_list()


func _refresh_container_list() -> void:
	if flow_manager == null:
		return
	container_list.clear()
	var containers: Array = flow_manager.get_containers()
	for c in containers:
		var index: int = int(c.get("index", 0))
		var name: String = c.get("name", "Unknown")
		var caps: Array = []
		if c.get("has_items", false):
			caps.append("I")
		if c.get("has_fluids", false):
			caps.append("F")
		if c.get("has_energy", false):
			caps.append("E")
		if c.get("has_redstone", false):
			caps.append("R")
		var display := "%d: %s [%s]" % [index, name, "".join(caps)]
		container_list.add_item(display)
	status_label.text = "Found %d containers" % containers.size()


# ============================================================
# Misc
# ============================================================

func _on_clear_pressed() -> void:
	_clear_graph()
	if flow_manager != null:
		# Clear by removing all nodes one by one.
		var nodes: Array = flow_manager.get_all_nodes()
		for node_info in nodes:
			flow_manager.remove_node(int(node_info.get("id", 0)))
	program_modified.emit()
	status_label.text = "Cleared all nodes"


func _clear_graph() -> void:
	for node_id in _nodes.keys():
		var node = _nodes[node_id]
		if is_instance_valid(node):
			node.queue_free()
	_nodes.clear()
	_connections.clear()
	# Clear graph connections.
	for conn in graph_edit.get_connection_list():
		graph_edit.disconnect_node(conn.from_node, conn.from_port,
								   conn.to_node, conn.to_port)
