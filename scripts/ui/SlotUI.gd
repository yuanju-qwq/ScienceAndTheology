class_name SlotUI extends Control

signal clicked(index: int, button: int)
signal right_clicked(index: int)
signal drag_started(index: int)

@export var slot_index: int = 0
@export var bg_color: Color = Color(0.15, 0.15, 0.18, 1.0)
@export var highlight_color: Color = Color(0.35, 0.55, 0.75, 1.0)

var item_stack: ItemStack:
	set(v):
		item_stack = v
		_update_ui()

@onready var bg := $Background as ColorRect
@onready var icon := $Icon as TextureRect
@onready var amount_label := $Amount as Label

func _ready() -> void:
	custom_minimum_size = Vector2(36, 36)
	size = Vector2(36, 36)
	_update_ui()

func _update_ui() -> void:
	if not is_inside_tree():
		return
	if item_stack == null or item_stack.is_empty():
		icon.texture = null
		amount_label.text = ""
		bg.color = bg_color
	else:
		icon.texture = item_stack.get_icon()
		amount_label.text = str(item_stack.count) if item_stack.count > 1 else ""
		bg.color = bg_color

func set_highlight(h: bool) -> void:
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
