# OrganSlotUI — displays a single organ slot in the sublimation panel
extends Control

@onready var slot_icon: TextureRect = $SlotIcon
@onready var slot_name: Label = $SlotName
@onready var sublimation_bar: ProgressBar = $SublimationBar
@onready var element_label: Label = $ElementLabel
@onready var quality_label: Label = $QualityLabel
@onready var level_label: Label = $LevelLabel

var slot_index: int = 0
var source_law_data: Resource = null

# Slot names matching OrganSlot enum
var SLOT_NAMES: Array = [
	"Heart", "Bone", "Blood", "Lung", "Eye", "Nerve", "Skin"
]

# Element names matching RuneElement enum
var ELEMENT_NAMES: Array = [
	"Fire", "Water", "Earth", "Air", "Light", "Dark", "Order", "Chaos"
]

# Quality names matching OrganQuality enum
var QUALITY_NAMES: Array = [
	"Flawed", "Common", "Good", "Pure", "Ancient", "Perfect"
]

# Quality colors for visual feedback
var QUALITY_COLORS: Array = [
	Color.GRAY, Color.WHITE, Color.GREEN, Color.CYAN, Color.MEDIUM_PURPLE, Color.GOLD
]


func _ready() -> void:
	slot_name.text = SLOT_NAMES[slot_index] if slot_index < SLOT_NAMES.size() else "Unknown"
	_refresh_display()


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
