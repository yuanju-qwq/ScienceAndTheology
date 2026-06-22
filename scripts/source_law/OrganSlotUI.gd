class_name OrganSlotUI extends Control

var slot_index: int = 0
var source_law_data: Resource = null

var SLOT_NAMES: Array = [
	"Heart", "Bone", "Blood", "Lung", "Eye", "Nerve", "Skin"
]

var ELEMENT_NAMES: Array = [
	"Fire", "Water", "Earth", "Air", "Light", "Dark", "Order", "Chaos"
]

var QUALITY_NAMES: Array = [
	"Flawed", "Common", "Good", "Pure", "Ancient", "Perfect"
]

var QUALITY_COLORS: Array = [
	Color.GRAY, Color.WHITE, Color.GREEN, Color.CYAN, Color.MEDIUM_PURPLE, Color.GOLD
]

var slot_icon: TextureRect
var slot_name: Label
var sublimation_bar: ProgressBar
var element_label: Label
var quality_label: Label
var level_label: Label


func _ready() -> void:
	_build_ui()
	slot_name.text = SLOT_NAMES[slot_index] if slot_index < SLOT_NAMES.size() else "Unknown"
	_refresh_display()


func _build_ui() -> void:
	custom_minimum_size = Vector2(420, 28)
	size = Vector2(420, 28)

	var hbox := HBoxContainer.new()
	hbox.anchors_preset = Control.PRESET_FULL_RECT
	add_child(hbox)

	slot_icon = TextureRect.new()
	slot_icon.name = "SlotIcon"
	slot_icon.custom_minimum_size = Vector2(24, 24)
	slot_icon.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	slot_icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(slot_icon)

	slot_name = Label.new()
	slot_name.name = "SlotName"
	slot_name.custom_minimum_size = Vector2(70, 0)
	slot_name.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	slot_name.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(slot_name)

	element_label = Label.new()
	element_label.name = "ElementLabel"
	element_label.custom_minimum_size = Vector2(60, 0)
	element_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(element_label)

	quality_label = Label.new()
	quality_label.name = "QualityLabel"
	quality_label.custom_minimum_size = Vector2(70, 0)
	quality_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(quality_label)

	level_label = Label.new()
	level_label.name = "LevelLabel"
	level_label.custom_minimum_size = Vector2(50, 0)
	level_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(level_label)

	sublimation_bar = ProgressBar.new()
	sublimation_bar.name = "SublimationBar"
	sublimation_bar.custom_minimum_size = Vector2(100, 0)
	sublimation_bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	sublimation_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hbox.add_child(sublimation_bar)


func setup(index: int, data: Resource) -> void:
	slot_index = index
	source_law_data = data
	_refresh_display()


func _refresh_display() -> void:
	if source_law_data == null:
		return

	var organ: Dictionary = source_law_data.get_organ(slot_index)
	var is_sublimated: bool = organ.get("is_sublimated", false)

	if is_sublimated:
		var elem_idx: int = organ.get("primary_element", 0)
		var quality_idx: int = organ.get("quality", 1)
		var level: int = organ.get("level", 0)
		var degree: int = organ.get("sublimation_degree", 0)

		element_label.text = ELEMENT_NAMES[elem_idx] if elem_idx < ELEMENT_NAMES.size() else "?"
		quality_label.text = QUALITY_NAMES[quality_idx] if quality_idx < QUALITY_NAMES.size() else "?"
		quality_label.add_theme_color_override(
			"font_color",
			QUALITY_COLORS[quality_idx] if quality_idx < QUALITY_COLORS.size() else Color.WHITE
		)
		level_label.text = "Lv %d" % level
		sublimation_bar.value = degree
		sublimation_bar.max_value = 10
		sublimation_bar.visible = true
		element_label.visible = true
		quality_label.visible = true
		level_label.visible = true
	else:
		sublimation_bar.visible = false
		element_label.visible = false
		quality_label.text = "Normal"
		quality_label.add_theme_color_override("font_color", Color.GRAY)
		level_label.text = ""