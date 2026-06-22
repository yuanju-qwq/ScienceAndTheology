class_name CombatStatsPanel extends Control

var player: PlayerController = null
var _stat_labels: Dictionary = {}


func _ready() -> void:
	_build_ui()


func _build_ui() -> void:
	var margin := 8
	var col_w := 220

	var main_hbox := HBoxContainer.new()
	main_hbox.position = Vector2(margin, margin)
	main_hbox.size = Vector2(size.x - margin * 2, size.y - margin * 2)
	add_child(main_hbox)

	var col1 := VBoxContainer.new()
	col1.custom_minimum_size = Vector2(col_w, 0)
	col1.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	main_hbox.add_child(col1)

	var col2 := VBoxContainer.new()
	col2.custom_minimum_size = Vector2(col_w, 0)
	col2.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	main_hbox.add_child(col2)

	var stats_left := [
		"health_max", "mana_max", "physical_attack", "magic_power",
		"physical_defense", "element_resistance"
	]
	var stats_right := [
		"move_speed", "attack_speed", "cast_speed",
		"crit_rate", "crit_damage", "dodge_rate",
		"health_regen", "mana_regen"
	]

	_add_stats_to(col1, stats_left)
	_add_stats_to(col2, stats_right)


func _add_stats_to(container: VBoxContainer, stat_keys: Array) -> void:
	for key in stat_keys:
		var row := HBoxContainer.new()
		row.custom_minimum_size = Vector2(0, 26)

		var name_label := Label.new()
		name_label.text = _stat_display_name(key)
		name_label.custom_minimum_size = Vector2(110, 0)
		name_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		row.add_child(name_label)

		var value_label := Label.new()
		value_label.name = "Value_%s" % key
		value_label.text = "..."
		value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		value_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		value_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		row.add_child(value_label)

		_stat_labels[key] = value_label
		container.add_child(row)


func _stat_display_name(key: String) -> String:
	match key:
		"health_max": return "Max Health"
		"mana_max": return "Max Mana"
		"physical_attack": return "Phys Attack"
		"magic_power": return "Magic Power"
		"physical_defense": return "Phys Defense"
		"element_resistance": return "Elem Resist"
		"move_speed": return "Move Speed"
		"attack_speed": return "Atk Speed"
		"cast_speed": return "Cast Speed"
		"crit_rate": return "Crit Rate"
		"crit_damage": return "Crit Damage"
		"dodge_rate": return "Dodge Rate"
		"health_regen": return "HP Regen"
		"mana_regen": return "MP Regen"
	return key


func setup(p: PlayerController) -> void:
	player = p
	refresh()


func refresh() -> void:
	if player == null:
		return
	var data = player.get_source_law_data()
	if data == null:
		return
	var attrs: Dictionary = data.compute_combat_attributes()
	for key in _stat_labels:
		var val = attrs.get(key, 0)
		var text := ""
		match key:
			"crit_rate", "crit_damage", "dodge_rate":
				text = "%.1f%%" % (float(val) * 100.0)
			"health_regen", "mana_regen":
				text = "%.1f" % float(val)
			"health_max", "mana_max":
				text = "%d" % int(val)
			_:
				text = "%.1f" % float(val)
		_stat_labels[key].text = text