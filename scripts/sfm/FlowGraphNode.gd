# FlowGraphNode — a single node in the flow editor graph.
#
# Wraps a Godot GraphNode, mapping SFM port types to slot colors and
# providing parameter editing widgets. Each instance is tagged with
# its FlowNodeId so the editor can sync changes back to GDFlowManager.
extends GraphNode
class_name FlowGraphNode

## Emitted when a node parameter changes.
signal param_changed(node_id: int, key: String, value: String)

## Emitted when the node is moved in the editor.
signal position_changed(node_id: int, x: float, y: float)

var node_id: int = 0
var node_type: int = 0

# Port type → slot color mapping for visual distinction.
const PORT_COLORS := {
	0: Color.WHITE,       # NONE
	1: Color.WHITE,       # FLOW (execution)
	2: Color(0.9, 0.7, 0.3),  # ITEM_STREAM (gold)
	3: Color(0.3, 0.5, 0.9),  # FLUID_STREAM (blue)
	4: Color(0.9, 0.3, 0.3),  # ENERGY (red)
	5: Color(0.9, 0.2, 0.9),  # REDSTONE (magenta)
	6: Color(0.3, 0.9, 0.3),  # NUMBER (green)
	7: Color(0.7, 0.7, 0.7),  # STRING (gray)
	8: Color(0.9, 0.9, 0.2),  # BOOLEAN (yellow)
}

# Port type → Godot slot type integer (for icon distinction).
const PORT_SLOT_TYPES := {
	0: 0,  # NONE
	1: 1,  # FLOW
	2: 2,  # ITEM_STREAM
	3: 3,  # FLUID_STREAM
	4: 4,  # ENERGY
	5: 5,  # REDSTONE
	6: 6,  # NUMBER
	7: 7,  # STRING
	8: 8,  # BOOLEAN
}


func _ready() -> void:
	draggable = true
	selectable = true
	resizable = false
	position_offset_changed.connect(_on_position_changed)


## Configure the node from a GDFlowManager node info dictionary.
func setup_from_info(info: Dictionary) -> void:
	node_id = int(info.get("id", 0))
	node_type = int(info.get("type", 0))
	title = GDFlowManager.get_node_type_name(node_type)
	position_offset = Vector2(float(info.get("x", 0.0)), float(info.get("y", 0.0)))

	# Clear existing slots.
	clear_all_slots()

	# Build input port slots.
	var input_ports: Array = info.get("input_ports", [])
	for i in range(input_ports.size()):
		var port: Dictionary = input_ports[i]
		var port_type: int = int(port.get("type", 0))
		var port_name: String = port.get("name", "")
		add_label_text(port_name, port_type, true, i)

	# Build output port slots.
	var output_ports: Array = info.get("output_ports", [])
	for i in range(output_ports.size()):
		var port: Dictionary = output_ports[i]
		var port_type: int = int(port.get("type", 0))
		var port_name: String = port.get("name", "")
		add_label_text(port_name, port_type, false, i)

	# Apply params as editable widgets.
	var params: Dictionary = info.get("params", {})
	build_param_widgets(params)


## Add a row with a label and optional editable field for a port.
func add_label_text(label_text: String, port_type: int, is_input: bool, slot_index: int) -> void:
	var hbox := HBoxContainer.new()
	var label := Label.new()
	label.text = label_text
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hbox.add_child(label)
	add_child(hbox)

	# Set slot properties for connection drawing.
	var color: Color = PORT_COLORS.get(port_type, Color.WHITE)
	var slot_type: int = PORT_SLOT_TYPES.get(port_type, 0)
	set_slot(slot_index, is_input, slot_type, color,
			 not is_input, slot_type, color)


## Build parameter editing widgets based on node type and params.
func build_param_widgets(params: Dictionary) -> void:
	# Different node types have different editable params.
	match node_type:
		GDFlowManager.NODE_TRIGGER_TIMER:
			add_param_spinbox("interval_ticks", "Interval (ticks)",
				int(params.get("interval_ticks", "20")), 1, 1200)
		GDFlowManager.NODE_TRIGGER_REDSTONE:
			add_param_spinbox("signal_threshold", "Signal Threshold",
				int(params.get("signal_threshold", "1")), 0, 15)
		GDFlowManager.NODE_ITEM_INPUT, GDFlowManager.NODE_ITEM_OUTPUT:
			add_param_spinbox("container_index", "Container Index",
				int(params.get("container_index", "1")), 1, 64)
			add_param_spinbox("max_items", "Max Items",
				int(params.get("max_items", "64")), 1, 99999)
		GDFlowManager.NODE_FLUID_INPUT, GDFlowManager.NODE_FLUID_OUTPUT:
			add_param_spinbox("container_index", "Container Index",
				int(params.get("container_index", "1")), 1, 64)
			add_param_spinbox("max_mb", "Max mB",
				int(params.get("max_mb", "1000")), 1, 999999)
		GDFlowManager.NODE_ENERGY_INPUT, GDFlowManager.NODE_ENERGY_OUTPUT:
			add_param_spinbox("container_index", "Container Index",
				int(params.get("container_index", "1")), 1, 64)
			add_param_spinbox("max_eu", "Max EU",
				int(params.get("max_eu", "1000")), 1, 999999999)
		GDFlowManager.NODE_REDSTONE_INPUT, GDFlowManager.NODE_REDSTONE_OUTPUT:
			add_param_spinbox("container_index", "Container Index",
				int(params.get("container_index", "1")), 1, 64)
		GDFlowManager.NODE_CONDITION:
			add_param_lineedit("expression", "Expression",
				params.get("expression", ""))
		GDFlowManager.NODE_LOOP:
			add_param_spinbox("iterations", "Iterations",
				int(params.get("iterations", "10")), 1, 99999)
		GDFlowManager.NODE_VARIABLE_GET, GDFlowManager.NODE_VARIABLE_SET:
			add_param_spinbox("variable_id", "Variable ID",
				int(params.get("variable_id", "1")), 1, 99999)
		GDFlowManager.NODE_MATH:
			add_param_lineedit("expression", "Expression",
				params.get("expression", "a + b"))
		GDFlowManager.NODE_TEXT_LABEL:
			add_param_lineedit("text", "Text",
				params.get("text", ""))
		GDFlowManager.NODE_COUNT:
			add_param_spinbox("container_index", "Container Index",
				int(params.get("container_index", "1")), 1, 64)


## Add a labeled SpinBox parameter row.
func add_param_spinbox(key: String, label_text: String, value: int, min_val: int, max_val: int) -> void:
	var hbox := HBoxContainer.new()
	var label := Label.new()
	label.text = label_text
	label.custom_minimum_size.x = 100.0
	hbox.add_child(label)
	var spin := SpinBox.new()
	spin.min_value = min_val
	spin.max_value = max_val
	spin.value = value
	spin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	spin.value_changed.connect(func(new_val: float) -> void:
		param_changed.emit(node_id, key, str(int(new_val)))
	)
	hbox.add_child(spin)
	add_child(hbox)


## Add a labeled LineEdit parameter row.
func add_param_lineedit(key: String, label_text: String, value: String) -> void:
	var hbox := HBoxContainer.new()
	var label := Label.new()
	label.text = label_text
	label.custom_minimum_size.x = 100.0
	hbox.add_child(label)
	var line := LineEdit.new()
	line.text = value
	line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	line.text_changed.connect(func(new_text: String) -> void:
		param_changed.emit(node_id, key, new_text)
	)
	hbox.add_child(line)
	add_child(hbox)


func _on_position_changed() -> void:
	position_changed.emit(node_id, position_offset.x, position_offset.y)
