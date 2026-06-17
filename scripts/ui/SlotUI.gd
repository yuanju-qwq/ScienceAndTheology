class_name SlotUI extends Control

signal clicked(index: int, button: int)
signal right_clicked(index: int)
@export var slot_index: int = 0
@export var bg_color: Color = Color(0.15, 0.15, 0.18, 1.0)
@export var highlight_color: Color = Color(0.35, 0.55, 0.75, 1.0)

var item_stack: ItemStack:
	set(v):
		item_stack = v
		_update_ui()

@onready var bg: ColorRect = get_node_or_null("Background") as ColorRect
@onready var icon: TextureRect = get_node_or_null("Icon") as TextureRect
@onready var amount_label: Label = get_node_or_null("Amount") as Label

func _ready() -> void:
	custom_minimum_size = Vector2(36, 36)
	size = Vector2(36, 36)
	_ensure_nodes()
	_update_ui()

func _ensure_nodes() -> void:
	if bg == null:
		bg = ColorRect.new()
		bg.name = "Background"
		bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
		add_child(bg)
	if icon == null:
		icon = TextureRect.new()
		icon.name = "Icon"
		icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		add_child(icon)
	if amount_label == null:
		amount_label = Label.new()
		amount_label.name = "Amount"
		amount_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		amount_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		amount_label.vertical_alignment = VERTICAL_ALIGNMENT_BOTTOM
		add_child(amount_label)
	bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	icon.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	amount_label.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)

func _update_ui() -> void:
	if not is_inside_tree():
		return
	_ensure_nodes()
	if item_stack == null or item_stack.is_empty():
		icon.texture = null
		amount_label.text = ""
		bg.color = bg_color
	else:
		icon.texture = item_stack.get_icon()
		amount_label.text = str(item_stack.count) if item_stack.count > 1 else ""
		bg.color = bg_color

func set_highlight(h: bool) -> void:
	_ensure_nodes()
	if h:
		bg.color = highlight_color
	else:
		bg.color = bg_color

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		match event.button_index:
			MOUSE_BUTTON_LEFT:
				clicked.emit(slot_index, MOUSE_BUTTON_LEFT)
			MOUSE_BUTTON_RIGHT:
				right_clicked.emit(slot_index)
		accept_event()
