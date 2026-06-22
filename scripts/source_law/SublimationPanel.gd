class_name SublimationPanel extends Control

var source_law_data: Resource = null
var organ_slot_uis: Array[OrganSlotUI] = []

var source_bar: ProgressBar
var source_label: Label
var stability_bar: ProgressBar
var stability_label: Label
var mutation_bar: ProgressBar
var mutation_label: Label
var mana_bar: ProgressBar
var mana_label: Label
var path_label: Label
var level_label: Label
var organ_container: VBoxContainer
var skill_list: ItemList

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
	_build_ui()


func _build_ui() -> void:
	var margin := 8
	var label_w := 60
	var bar_w := 200

	var main_vbox := VBoxContainer.new()
	main_vbox.position = Vector2(margin, margin)
	main_vbox.size = Vector2(size.x - margin * 2, size.y - margin * 2)
	add_child(main_vbox)

	var header := HBoxContainer.new()
	main_vbox.add_child(header)

	path_label = Label.new()
	path_label.name = "PathLabel"
	path_label.custom_minimum_size = Vector2(160, 24)
	header.add_child(path_label)

	level_label = Label.new()
	level_label.name = "LevelLabel"
	level_label.custom_minimum_size = Vector2(80, 24)
	header.add_child(level_label)

	main_vbox.add_child(HSeparator.new())

	source_bar = _make_bar("SourceBar")
	source_label = _make_bar_label()
	var src_row := HBoxContainer.new()
	var src_bar_container := HBoxContainer.new()
	src_bar_container.add_child(source_label)
	src_bar_container.add_child(source_bar)
	var src_title := Label.new()
	src_title.text = "Source "
	src_title.custom_minimum_size = Vector2(label_w, 0)
	src_row.add_child(src_title)
	src_row.add_child(src_bar_container)
	main_vbox.add_child(src_row)

	stability_bar = _make_bar("StabilityBar")
	stability_label = _make_bar_label()
	var stb_row := HBoxContainer.new()
	var stb_bar_container := HBoxContainer.new()
	stb_bar_container.add_child(stability_label)
	stb_bar_container.add_child(stability_bar)
	var stb_title := Label.new()
	stb_title.text = "Stability "
	stb_title.custom_minimum_size = Vector2(label_w, 0)
	stb_row.add_child(stb_title)
	stb_row.add_child(stb_bar_container)
	main_vbox.add_child(stb_row)

	mutation_bar = _make_bar("MutationBar")
	mutation_label = _make_bar_label()
	var mut_row := HBoxContainer.new()
	var mut_bar_container := HBoxContainer.new()
	mut_bar_container.add_child(mutation_label)
	mut_bar_container.add_child(mutation_bar)
	var mut_title := Label.new()
	mut_title.text = "Mutation "
	mut_title.custom_minimum_size = Vector2(label_w, 0)
	mut_row.add_child(mut_title)
	mut_row.add_child(mut_bar_container)
	main_vbox.add_child(mut_row)

	mana_bar = _make_bar("ManaBar")
	mana_label = _make_bar_label()
	var mana_row := HBoxContainer.new()
	var mana_bar_container := HBoxContainer.new()
	mana_bar_container.add_child(mana_label)
	mana_bar_container.add_child(mana_bar)
	var mana_title := Label.new()
	mana_title.text = "Mana "
	mana_title.custom_minimum_size = Vector2(label_w, 0)
	mana_row.add_child(mana_title)
	mana_row.add_child(mana_bar_container)
	main_vbox.add_child(mana_row)

	main_vbox.add_child(HSeparator.new())

	var organ_header := Label.new()
	organ_header.text = "Organ Slots"
	main_vbox.add_child(organ_header)

	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(0, 140)
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	main_vbox.add_child(scroll)

	organ_container = VBoxContainer.new()
	organ_container.name = "OrganContainer"
	scroll.add_child(organ_container)

	main_vbox.add_child(HSeparator.new())

	var skill_header := Label.new()
	skill_header.text = "Skills"
	main_vbox.add_child(skill_header)

	skill_list = ItemList.new()
	skill_list.name = "SkillList"
	skill_list.custom_minimum_size = Vector2(0, 80)
	skill_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	main_vbox.add_child(skill_list)


func _make_bar(name: String) -> ProgressBar:
	var bar := ProgressBar.new()
	bar.name = name
	bar.custom_minimum_size = Vector2(200, 20)
	bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return bar


func _make_bar_label() -> Label:
	var lbl := Label.new()
	lbl.custom_minimum_size = Vector2(140, 0)
	lbl.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return lbl


func setup(data: Resource) -> void:
	source_law_data = data
	_setup_organ_slots()
	refresh()


func _setup_organ_slots() -> void:
	for child in organ_container.get_children():
		child.queue_free()
	organ_slot_uis.clear()

	for i in range(7):
		var slot_ui := OrganSlotUI.new()
		organ_container.add_child(slot_ui)
		slot_ui.setup(i, source_law_data)
		organ_slot_uis.append(slot_ui)


func refresh() -> void:
	if source_law_data == null:
		return

	var src_current: int = source_law_data.get_source_current()
	var src_max: int = source_law_data.get_source_max()
	source_bar.value = src_max
	source_bar.max_value = max(src_max, 1)
	source_label.text = "%d / %d" % [src_current, src_max]

	var stability: float = source_law_data.get_stability()
	stability_bar.value = stability
	stability_bar.max_value = 100.0
	stability_label.text = "%.0f%%" % stability

	var mutation: float = source_law_data.get_mutation()
	mutation_bar.value = mutation
	mutation_bar.max_value = 100.0
	var stage_idx: int = _get_mutation_stage(mutation)
	mutation_label.text = "%.0f%% (%s)" % [mutation, MUTATION_STAGE_NAMES[stage_idx]]

	var mana_current: int = source_law_data.get_mana_current()
	var mana_max: int = source_law_data.get_mana_max()
	mana_bar.value = mana_current
	mana_bar.max_value = max(mana_max, 1)
	mana_label.text = "%d / %d" % [mana_current, mana_max]

	var path_id: int = source_law_data.get_path_id()
	var sub_level: int = source_law_data.get_sublimation_level()
	path_label.text = "Path: %s" % PATH_NAMES.get(path_id, "Unknown")
	level_label.text = "Level: %d" % sub_level

	for slot_ui in organ_slot_uis:
		if slot_ui is OrganSlotUI:
			slot_ui.setup(slot_ui.slot_index, source_law_data)

	skill_list.clear()
	var skills: Array = source_law_data.get_available_skills()
	for skill_dict in skills:
		var name: String = tr(skill_dict.get("title_key", "ui.unknown"))
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