# SublimationPanel — main panel for the player's source law data
extends Control

@onready var organ_container: VBoxContainer = $ScrollContainer/OrganContainer
@onready var source_bar: ProgressBar = $SourceBar
@onready var source_label: Label = $SourceLabel
@onready var stability_bar: ProgressBar = $StabilityBar
@onready var stability_label: Label = $StabilityLabel
@onready var mutation_bar: ProgressBar = $MutationBar
@onready var mutation_label: Label = $MutationLabel
@onready var mana_bar: ProgressBar = $ManaBar
@onready var mana_label: Label = $ManaLabel
@onready var path_label: Label = $PathLabel
@onready var level_label: Label = $LevelLabel
@onready var skill_list: ItemList = $SkillList

var source_law_data: Resource = null
var organ_slot_uis: Array = []

var PATH_NAMES: Dictionary = {
	0: "Mortal",
	1: "Sand Armor",
	2: "Tidal",
	3: "Storm",
	4: "Furnace",
	5: "Radiance",
}

var MUTATION_STAGE_NAMES: Array = [
	"Normal", "Mild Pollution", "Symptoms", "Severe Mutation", "Source Runaway"
]


func _ready() -> void:
	_setup_organ_slots()


func setup(data: Resource) -> void:
	source_law_data = data
	refresh()


func _setup_organ_slots() -> void:
	for child in organ_container.get_children():
		child.queue_free()
	organ_slot_uis.clear()

	for i in range(7):
		var slot_ui: Control = Control.new()
		slot_ui.set_script(preload("res://scripts/source_law/OrganSlotUI.gd"))
		organ_container.add_child(slot_ui)
		slot_ui.setup(i, source_law_data)
		organ_slot_uis.append(slot_ui)


func refresh() -> void:
	if source_law_data == null:
		return

	# Source reserve
	var src_current: int = source_law_data.get_source_current()
	var src_max: int = source_law_data.get_source_max()
	source_bar.value = src_max
	source_bar.max_value = max(src_max, 1)
	source_label.text = "Source: %d / %d" % [src_current, src_max]

	# Stability
	var stability: float = source_law_data.get_stability()
	stability_bar.value = stability
	stability_bar.max_value = 100.0
	stability_label.text = "Stability: %.0f%%" % stability

	# Mutation
	var mutation: float = source_law_data.get_mutation()
	mutation_bar.value = mutation
	mutation_bar.max_value = 100.0
	var stage_idx: int = _get_mutation_stage(mutation)
	mutation_label.text = "Mutation: %.0f%% (%s)" % [mutation, MUTATION_STAGE_NAMES[stage_idx]]

	# Mana
	var mana_current: int = source_law_data.get_mana_current()
	var mana_max: int = source_law_data.get_mana_max()
	mana_bar.value = mana_current
	mana_bar.max_value = max(mana_max, 1)
	mana_label.text = "Mana: %d / %d" % [mana_current, mana_max]

	# Path & level
	var path_id: int = source_law_data.get_path_id()
	var sub_level: int = source_law_data.get_sublimation_level()
	path_label.text = "Path: %s" % PATH_NAMES.get(path_id, "Unknown")
	level_label.text = "Level: %d" % sub_level

	# Organ slots
	for slot_ui in organ_slot_uis:
		if slot_ui.has_method("setup"):
			slot_ui.setup(slot_ui.slot_index, source_law_data)

	# Skills
	skill_list.clear()
	var skills: Array = source_law_data.get_available_skills()
	for skill_dict in skills:
		var name: String = skill_dict.get("display_name", "?")
		var cost: int = skill_dict.get("mana_cost", 0)
		skill_list.add_item("%s (Mana: %d)" % [name, cost])


func _get_mutation_stage(mutation: float) -> int:
	if mutation <= 20.0:
		return 0
	elif mutation <= 40.0:
		return 1
	elif mutation <= 60.0:
		return 2
	elif mutation <= 80.0:
		return 3
	else:
		return 4
