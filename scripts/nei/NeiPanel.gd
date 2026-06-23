class_name NeiPanel
extends Control

signal closed
signal opened

const SLOT_SIZE := 36
const GRID_COLS := 8
const ITEMS_PER_PAGE := 96
const RECIPES_PER_PAGE := 5
const PANEL_SIZE := Vector2(960, 640)
const LEFT_W := 340
const MACHINE_FILTER_ALL := "__all__"
const MACHINE_FILTER_CRAFTING := "__crafting__"

var player: PlayerController = null
var _is_open := false
var _current_item_id := 0
var _hovered_item_id := 0
var _detail_mode := "info"
var _item_ids: Array[int] = []
var _filtered_ids: Array[int] = []
var _item_page := 0
var _recipe_page := 0
var _machine_filter := MACHINE_FILTER_ALL

var _search_box: LineEdit
var _search_help: Label
var _grid: GridContainer
var _item_page_label: Label
var _item_prev_btn: Button
var _item_next_btn: Button
var _icon: TextureRect
var _title: Label
var _subtitle: Label
var _tabs: HBoxContainer
var _machine_filter_box: OptionButton
var _list: VBoxContainer


func _ready() -> void:
	visible = false
	mouse_filter = Control.MOUSE_FILTER_STOP
	_build_ui()
	get_viewport().size_changed.connect(_center)


func set_player(p: PlayerController) -> void:
	player = p


func toggle() -> void:
	if _is_open:
		close()
	else:
		open()


func open() -> void:
	_is_open = true
	visible = true
	_refresh()
	opened.emit()


func close() -> void:
	_is_open = false
	visible = false
	closed.emit()


func open_for_item(item_id: int, mode: String = "recipes") -> void:
	if not _is_open:
		open()
	_detail_mode = mode
	_recipe_page = 0
	_select_item(item_id)
	_search_box.clear()


func _input(event: InputEvent) -> void:
	if not _is_open or not visible:
		return
	if not (event is InputEventKey):
		return
	var key_event := event as InputEventKey
	if not key_event.pressed or key_event.echo:
		return
	var target_item := _hovered_item_id if _hovered_item_id > 0 else _current_item_id
	if target_item <= 0:
		return
	match key_event.keycode:
		KEY_R:
			_detail_mode = "recipes"
			_recipe_page = 0
			_select_item(target_item)
			get_viewport().set_input_as_handled()
		KEY_U:
			_detail_mode = "usage"
			_recipe_page = 0
			_select_item(target_item)
			get_viewport().set_input_as_handled()
		KEY_M:
			_detail_mode = "machines"
			_recipe_page = 0
			_select_item(target_item)
			get_viewport().set_input_as_handled()


func _center() -> void:
	position = (get_viewport_rect().size - size) / 2.0


func _build_ui() -> void:
	size = PANEL_SIZE
	_center()

	var bg := ColorRect.new()
	bg.size = size
	bg.color = Color(0.055, 0.058, 0.07, 0.96)
	add_child(bg)

	var header := Label.new()
	header.text = "NEI Content Browser"
	header.position = Vector2(8, 4)
	header.size = Vector2(320, 24)
	header.add_theme_color_override("font_color", Color(0.82, 0.90, 1.0))
	add_child(header)

	var close_btn := Button.new()
	close_btn.text = "X"
	close_btn.position = Vector2(size.x - 30, 2)
	close_btn.size = Vector2(26, 26)
	close_btn.pressed.connect(close)
	add_child(close_btn)

	_search_box = LineEdit.new()
	_search_box.position = Vector2(8, 38)
	_search_box.size = Vector2(LEFT_W - 16, 26)
	_search_box.placeholder_text = "Search: iron  @macerator  fluid:steam  tier:lv"
	_search_box.text_changed.connect(_on_search)
	add_child(_search_box)

	_search_help = Label.new()
	_search_help.position = Vector2(8, 66)
	_search_help.size = Vector2(LEFT_W - 16, 20)
	_search_help.text = "Tokens: id:12 key:plate @machine fluid:water tier:lv | R/U/M on hovered item"
	_search_help.add_theme_color_override("font_color", Color(0.55, 0.63, 0.72))
	_search_help.add_theme_font_size_override("font_size", 10)
	add_child(_search_help)

	var scroll := ScrollContainer.new()
	scroll.position = Vector2(8, 92)
	scroll.size = Vector2(LEFT_W - 16, size.y - 132)
	add_child(scroll)

	_grid = GridContainer.new()
	_grid.columns = GRID_COLS
	_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_grid)

	var pager := HBoxContainer.new()
	pager.position = Vector2(8, size.y - 34)
	pager.size = Vector2(LEFT_W - 16, 28)
	add_child(pager)

	_item_prev_btn = Button.new()
	_item_prev_btn.text = "<"
	_item_prev_btn.pressed.connect(_on_item_page_delta.bind(-1))
	pager.add_child(_item_prev_btn)

	_item_page_label = Label.new()
	_item_page_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_item_page_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_item_page_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	pager.add_child(_item_page_label)

	_item_next_btn = Button.new()
	_item_next_btn.text = ">"
	_item_next_btn.pressed.connect(_on_item_page_delta.bind(1))
	pager.add_child(_item_next_btn)

	_build_detail_panel()


func _build_detail_panel() -> void:
	var dx := LEFT_W + 8
	var dw := size.x - dx - 8
	var panel := Panel.new()
	panel.position = Vector2(dx, 38)
	panel.size = Vector2(dw, size.y - 46)
	add_child(panel)

	_icon = TextureRect.new()
	_icon.position = Vector2(10, 10)
	_icon.size = Vector2(42, 42)
	_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	panel.add_child(_icon)

	_title = Label.new()
	_title.position = Vector2(62, 8)
	_title.size = Vector2(dw - 72, 26)
	_title.add_theme_color_override("font_color", Color(0.94, 0.97, 1.0))
	_title.add_theme_font_size_override("font_size", 18)
	panel.add_child(_title)

	_subtitle = Label.new()
	_subtitle.position = Vector2(62, 34)
	_subtitle.size = Vector2(dw - 72, 20)
	_subtitle.add_theme_color_override("font_color", Color(0.62, 0.70, 0.82))
	panel.add_child(_subtitle)

	_tabs = HBoxContainer.new()
	_tabs.position = Vector2(8, 64)
	_tabs.size = Vector2(dw - 16, 28)
	panel.add_child(_tabs)

	_machine_filter_box = OptionButton.new()
	_machine_filter_box.position = Vector2(8, 96)
	_machine_filter_box.size = Vector2(dw - 16, 28)
	_machine_filter_box.item_selected.connect(_on_machine_filter_selected)
	panel.add_child(_machine_filter_box)

	var detail_scroll := ScrollContainer.new()
	detail_scroll.position = Vector2(8, 132)
	detail_scroll.size = Vector2(dw - 16, panel.size.y - 140)
	panel.add_child(detail_scroll)

	_list = VBoxContainer.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	detail_scroll.add_child(_list)


func _refresh() -> void:
	NEIIndex.ensure_built()
	_populate_machine_filter()
	_item_ids = NEIIndex.get_all_item_ids()
	_filtered_ids = _item_ids.duplicate()
	_item_page = 0
	_build_grid_current_page()
	if _current_item_id <= 0 and not _filtered_ids.is_empty():
		_select_item(_filtered_ids[0])
	elif _current_item_id > 0:
		_select_item(_current_item_id)


func _populate_machine_filter() -> void:
	if _machine_filter_box == null:
		return
	var current := _machine_filter
	_machine_filter_box.clear()
	_machine_filter_box.add_item("All recipe types")
	_machine_filter_box.set_item_metadata(0, MACHINE_FILTER_ALL)
	_machine_filter_box.add_item("Crafting only")
	_machine_filter_box.set_item_metadata(1, MACHINE_FILTER_CRAFTING)
	for machine in NEIIndex.get_machine_types():
		_machine_filter_box.add_item("Machine: %s" % machine)
		_machine_filter_box.set_item_metadata(_machine_filter_box.get_item_count() - 1, str(machine))

	var selected_index := 0
	for i in range(_machine_filter_box.get_item_count()):
		if str(_machine_filter_box.get_item_metadata(i)) == current:
			selected_index = i
			break
	_machine_filter_box.select(selected_index)
	_machine_filter = str(_machine_filter_box.get_item_metadata(selected_index))


func _build_grid_current_page() -> void:
	for child in _grid.get_children():
		child.queue_free()

	var total_pages := _item_total_pages()
	_item_page = clampi(_item_page, 0, total_pages - 1)
	var first := _item_page * ITEMS_PER_PAGE
	var last := mini(first + ITEMS_PER_PAGE, _filtered_ids.size())
	for i in range(first, last):
		var item_id := _filtered_ids[i]
		_grid.add_child(_make_item_slot(item_id, 1))
	_update_item_pager()


func _make_item_slot(item_id: int, count: int = 1) -> SlotUI:
	var slot := SlotUI.new()
	slot.slot_index = item_id
	slot.custom_minimum_size = Vector2(SLOT_SIZE, SLOT_SIZE)
	slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
	slot.item_stack = ItemStack.new(item_id, count)
	slot.clicked.connect(_on_slot_clicked)
	slot.right_clicked.connect(_on_slot_right_clicked)
	slot.mouse_entered.connect(_on_item_hovered.bind(item_id))
	slot.mouse_exited.connect(_on_item_unhovered.bind(item_id))
	return slot


func _item_total_pages() -> int:
	if _filtered_ids.is_empty():
		return 1
	return maxi(1, int(ceil(float(_filtered_ids.size()) / float(ITEMS_PER_PAGE))))


func _update_item_pager() -> void:
	var total_pages := _item_total_pages()
	_item_prev_btn.disabled = _item_page <= 0
	_item_next_btn.disabled = _item_page >= total_pages - 1
	_item_page_label.text = "%d items | page %d / %d" % [_filtered_ids.size(), _item_page + 1, total_pages]


func _on_item_page_delta(delta: int) -> void:
	_item_page = clampi(_item_page + delta, 0, _item_total_pages() - 1)
	_build_grid_current_page()


func _on_slot_clicked(item_id: int, button: int) -> void:
	if _try_creative_pick(item_id, button):
		return
	_detail_mode = "recipes"
	_recipe_page = 0
	_select_item(item_id)


func _on_slot_right_clicked(item_id: int) -> void:
	_detail_mode = "usage"
	_recipe_page = 0
	_select_item(item_id)


func _select_item(item_id: int) -> void:
	_current_item_id = item_id
	var def = ItemDatabase.get_item(item_id)
	if def == null:
		_icon.texture = null
		_title.text = "Item #%d" % item_id
		_subtitle.text = "Unregistered item"
	else:
		_icon.texture = def.icon
		_title.text = tr(def.title_key)
		_subtitle.text = _subtitle_for(item_id, def)
	_rebuild_tabs()
	_show_detail()


func _subtitle_for(item_id: int, def) -> String:
	var key := NEIIndex.get_item_key(item_id)
	var text := "ID %d" % item_id
	if not key.is_empty():
		text += "  |  %s" % key
	if def != null:
		text += "  |  Max %d" % int(def.max_stack)
	return text


func _rebuild_tabs() -> void:
	for child in _tabs.get_children():
		child.queue_free()
	_add_tab("info", "Info")
	_add_tab("recipes", "Recipes")
	_add_tab("usage", "Usage")
	_add_tab("machines", "Machines")


func _add_tab(mode: String, label_text: String) -> void:
	var btn := Button.new()
	btn.text = label_text
	btn.toggle_mode = true
	btn.button_pressed = (_detail_mode == mode)
	btn.pressed.connect(_on_tab_pressed.bind(mode))
	_tabs.add_child(btn)


func _on_tab_pressed(mode: String) -> void:
	_detail_mode = mode
	_recipe_page = 0
	_rebuild_tabs()
	_show_detail()


func _show_detail() -> void:
	_clear_detail()
	_machine_filter_box.visible = _detail_mode == "recipes" or _detail_mode == "usage"
	if _detail_mode == "info":
		_show_info()
	elif _detail_mode == "recipes":
		_show_recipes(NEIIndex.get_recipes_for_output(_current_item_id), "No recipes found.")
	elif _detail_mode == "usage":
		_show_recipes(NEIIndex.get_recipes_for_input(_current_item_id), "No usages found.")
	else:
		_show_machines()


func _clear_detail() -> void:
	for child in _list.get_children():
		child.queue_free()


func _show_info() -> void:
	var def = ItemDatabase.get_item(_current_item_id)
	if def == null:
		_add_text("This item id is not registered.", Color(0.85, 0.6, 0.6))
		return
	_add_section("Base")
	_add_kv("Item ID", str(_current_item_id))
	var key := NEIIndex.get_item_key(_current_item_id)
	_add_kv("Item Key", key if not key.is_empty() else "-")
	_add_kv("Max Stack", str(def.max_stack))
	_add_section("Recipe Summary")
	_add_kv("Recipes", str(NEIIndex.get_recipes_for_output(_current_item_id).size()))
	_add_kv("Usages", str(NEIIndex.get_recipes_for_input(_current_item_id).size()))
	_add_kv("Shortcuts", "Hover item + R/U/M. Creative: middle click or Shift+left gives one stack.")

	var related := _related_machine_text()
	if not related.is_empty():
		_add_kv("Machines", related)

	var tooltip_lines: Array = def.get_tooltip_lines()
	if not tooltip_lines.is_empty():
		_add_section("Tooltip")
		for line in tooltip_lines:
			_add_text(str(line), Color(0.78, 0.84, 0.92))


func _related_machine_text() -> String:
	var names := PackedStringArray()
	var seen := {}
	for ref in NEIIndex.get_related_recipes(_current_item_id):
		var name := ref.machine_type if not ref.machine_type.is_empty() else ref.recipe_type
		if name.is_empty() or seen.has(name):
			continue
		seen[name] = true
		names.append(name)
	names.sort()
	return ", ".join(names)


func _show_machines() -> void:
	var groups := _machine_groups_for_current_item()
	if groups.is_empty():
		_add_text("No related machines.", Color(0.62, 0.64, 0.72))
		return
	_add_section("Related Machines")
	for machine in groups.keys():
		var group: Dictionary = groups[machine]
		var card := PanelContainer.new()
		card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var box := VBoxContainer.new()
		box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		card.add_child(box)

		var title := Label.new()
		title.text = str(machine)
		title.add_theme_color_override("font_color", Color(0.55, 0.82, 1.0))
		box.add_child(title)

		var meta := Label.new()
		meta.text = "Recipes: %d | Usages: %d | Min tier: %s | EU/t: %s" % [
			int(group.get("recipes", 0)),
			int(group.get("usages", 0)),
			str(group.get("min_tier", "-")),
			str(group.get("max_eut", "-")),
		]
		meta.add_theme_color_override("font_color", Color(0.82, 0.78, 0.58))
		box.add_child(meta)

		var buttons := HBoxContainer.new()
		var recipe_btn := Button.new()
		recipe_btn.text = "Show Recipes"
		recipe_btn.pressed.connect(_on_machine_jump.bind(str(machine), "recipes"))
		buttons.add_child(recipe_btn)
		var usage_btn := Button.new()
		usage_btn.text = "Show Usages"
		usage_btn.pressed.connect(_on_machine_jump.bind(str(machine), "usage"))
		buttons.add_child(usage_btn)
		box.add_child(buttons)
		_list.add_child(card)


func _machine_groups_for_current_item() -> Dictionary:
	var groups := {}
	for mode in ["recipes", "usages"]:
		var refs: Array[NEIIndex.RecipeRef] = (
			NEIIndex.get_recipes_for_output(_current_item_id)
			if mode == "recipes"
			else NEIIndex.get_recipes_for_input(_current_item_id))
		for ref in refs:
			var machine := ref.machine_type if not ref.machine_type.is_empty() else ref.recipe_type
			if machine.is_empty():
				machine = "unknown"
			if not groups.has(machine):
				groups[machine] = {"recipes": 0, "usages": 0, "min_tier": "-", "max_eut": "-"}
			var group: Dictionary = groups[machine]
			group[mode] = int(group.get(mode, 0)) + 1
			if ref.min_tier > 0:
				var current_tier := int(group.get("min_tier", ref.min_tier)) if str(group.get("min_tier", "-")).is_valid_int() else ref.min_tier
				group["min_tier"] = mini(current_tier, ref.min_tier)
			if ref.eu_per_tick > 0:
				var current_eut := int(group.get("max_eut", 0)) if str(group.get("max_eut", "0")).is_valid_int() else 0
				group["max_eut"] = maxi(current_eut, ref.eu_per_tick)
			groups[machine] = group
	return groups


func _on_machine_jump(machine: String, mode: String) -> void:
	_machine_filter = machine if machine != "crafting" else MACHINE_FILTER_CRAFTING
	_populate_machine_filter()
	_detail_mode = mode
	_recipe_page = 0
	_rebuild_tabs()
	_show_detail()


func _show_recipes(refs: Array[NEIIndex.RecipeRef], empty_text: String) -> void:
	var filtered := _filter_recipes(refs)
	if filtered.is_empty():
		_add_text(empty_text, Color(0.62, 0.64, 0.72))
		return
	var total_pages := maxi(1, int(ceil(float(filtered.size()) / float(RECIPES_PER_PAGE))))
	_recipe_page = clampi(_recipe_page, 0, total_pages - 1)
	_add_recipe_pager(filtered.size(), total_pages)
	var first := _recipe_page * RECIPES_PER_PAGE
	var last := mini(first + RECIPES_PER_PAGE, filtered.size())
	for i in range(first, last):
		var ref: NEIIndex.RecipeRef = filtered[i]
		_list.add_child(_recipe_card(ref))
		var sep := HSeparator.new()
		sep.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		_list.add_child(sep)


func _filter_recipes(refs: Array[NEIIndex.RecipeRef]) -> Array[NEIIndex.RecipeRef]:
	if _machine_filter == MACHINE_FILTER_ALL:
		return refs.duplicate()
	var result: Array[NEIIndex.RecipeRef] = []
	for ref in refs:
		if _machine_filter == MACHINE_FILTER_CRAFTING:
			if ref.recipe_type == "crafting":
				result.append(ref)
		elif ref.machine_type == _machine_filter:
			result.append(ref)
	return result


func _add_recipe_pager(total: int, total_pages: int) -> void:
	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var prev := Button.new()
	prev.text = "Prev"
	prev.disabled = _recipe_page <= 0
	prev.pressed.connect(_on_recipe_page_delta.bind(-1))
	row.add_child(prev)
	var label := Label.new()
	label.text = "%d recipes | page %d / %d" % [total, _recipe_page + 1, total_pages]
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(label)
	var next := Button.new()
	next.text = "Next"
	next.disabled = _recipe_page >= total_pages - 1
	next.pressed.connect(_on_recipe_page_delta.bind(1))
	row.add_child(next)
	_list.add_child(row)
	var sep := HSeparator.new()
	sep.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_list.add_child(sep)


func _on_recipe_page_delta(delta: int) -> void:
	_recipe_page += delta
	_show_detail()


func _recipe_card(ref: NEIIndex.RecipeRef) -> Control:
	var card := PanelContainer.new()
	card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	card.add_child(box)

	var title := Label.new()
	title.text = _source_tag(ref)
	title.add_theme_color_override("font_color", Color(0.55, 0.82, 1.0))
	box.add_child(title)

	var meta := _recipe_meta(ref)
	if not meta.is_empty():
		var meta_label := Label.new()
		meta_label.text = meta
		meta_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		meta_label.add_theme_color_override("font_color", Color(0.82, 0.78, 0.58))
		box.add_child(meta_label)

	if ref.recipe_type == "crafting":
		_add_crafting_layout(box, ref)
	else:
		_add_linear_recipe_layout(box, ref)
	return card


func _add_linear_recipe_layout(box: VBoxContainer, ref: NEIIndex.RecipeRef) -> void:
	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_child(row)
	_add_stacks(row, ref.item_inputs, ref.fluid_inputs)
	var arrow := Label.new()
	arrow.text = "  ->  "
	arrow.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	arrow.custom_minimum_size.y = SLOT_SIZE
	row.add_child(arrow)
	_add_stacks(row, ref.item_outputs, ref.fluid_outputs)


func _add_crafting_layout(box: VBoxContainer, ref: NEIIndex.RecipeRef) -> void:
	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_child(row)

	var grid := GridContainer.new()
	grid.columns = 3
	row.add_child(grid)
	for entry in _crafting_grid_entries(ref):
		_add_grid_entry(grid, entry)

	var arrow := Label.new()
	arrow.text = "  ->  "
	arrow.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	arrow.custom_minimum_size.y = SLOT_SIZE * 3
	row.add_child(arrow)

	_add_stacks(row, ref.item_outputs, ref.fluid_outputs)


func _crafting_grid_entries(ref: NEIIndex.RecipeRef) -> Array:
	var data := ref.data
	var entries: Array = []
	for i in range(9):
		entries.append({})

	if data.has("grid") and data["grid"] is Array:
		_fill_grid_from_array(entries, data["grid"] as Array)
		return entries
	if data.has("slots") and data["slots"] is Array:
		_fill_grid_from_slots(entries, data["slots"] as Array)
		return entries
	if data.has("pattern") and data["pattern"] is Array:
		_fill_grid_from_pattern(entries, data["pattern"] as Array, data)
		return entries
	if data.has("shape") and data["shape"] is Array:
		_fill_grid_from_pattern(entries, data["shape"] as Array, data)
		return entries

	for i in range(mini(ref.item_inputs.size(), 9)):
		entries[i] = ref.item_inputs[i]
	return entries


func _fill_grid_from_array(entries: Array, raw: Array) -> void:
	for i in range(mini(raw.size(), 9)):
		var value = raw[i]
		if value is Dictionary:
			entries[i] = _normalize_grid_stack(value as Dictionary)
		elif int(value) > 0:
			entries[i] = {"item_id": int(value), "count": 1}


func _fill_grid_from_slots(entries: Array, raw: Array) -> void:
	for value in raw:
		if not (value is Dictionary):
			continue
		var slot := value as Dictionary
		var index := int(slot.get("slot", slot.get("index", -1)))
		if index < 0 or index >= 9:
			continue
		entries[index] = _normalize_grid_stack(slot)


func _fill_grid_from_pattern(entries: Array, pattern: Array, data: Dictionary) -> void:
	var key_map: Dictionary = data.get("keys", data.get("key", {}))
	for y in range(mini(pattern.size(), 3)):
		var row := str(pattern[y])
		for x in range(mini(row.length(), 3)):
			var ch := row.substr(x, 1)
			if ch == " " or ch == ".":
				continue
			var raw = key_map.get(ch, {})
			if raw is Dictionary:
				entries[y * 3 + x] = _normalize_grid_stack(raw as Dictionary)


func _normalize_grid_stack(raw: Dictionary) -> Dictionary:
	var item_id := int(raw.get("item_id", 0))
	if item_id <= 0 and raw.has("item_key"):
		item_id = _item_id_from_key(str(raw.get("item_key", "")))
	if item_id <= 0:
		return {}
	return {"item_id": item_id, "count": int(raw.get("amount", raw.get("count", 1)))}


func _item_id_from_key(item_key: String) -> int:
	if item_key.is_empty():
		return -1
	if ItemDatabase.has_method("get_item_id_by_key"):
		return int(ItemDatabase.get_item_id_by_key(item_key))
	return -1


func _add_grid_entry(parent: GridContainer, entry: Dictionary) -> void:
	if entry.is_empty():
		var empty := Panel.new()
		empty.custom_minimum_size = Vector2(SLOT_SIZE, SLOT_SIZE)
		parent.add_child(empty)
		return
	var item_id := int(entry.get("item_id", 0))
	var slot := _make_item_slot(item_id, int(entry.get("count", 1)))
	parent.add_child(slot)


func _recipe_meta(ref: NEIIndex.RecipeRef) -> String:
	var parts := PackedStringArray()
	if not ref.tools.is_empty():
		parts.append("Tools: %s" % ", ".join(ref.tools))
	if not ref.conditions.is_empty():
		var condition_parts := PackedStringArray()
		for key in ref.conditions.keys():
			condition_parts.append("%s=%s" % [str(key), str(ref.conditions[key])])
		parts.append("Conditions: %s" % ", ".join(condition_parts))
	return " | ".join(parts)


func _add_stacks(parent: HBoxContainer, items: Array[Dictionary], fluids: Array[Dictionary]) -> void:
	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(row)
	for entry in items:
		var item_id := int(entry.get("item_id", 0))
		if item_id <= 0:
			continue
		var box := VBoxContainer.new()
		var slot := _make_item_slot(item_id, int(entry.get("count", 1)))
		box.add_child(slot)
		var probability := float(entry.get("probability", 1.0))
		if probability < 0.999:
			var pct := Label.new()
			pct.text = "%d%%" % int(round(probability * 100.0))
			pct.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
			pct.add_theme_color_override("font_color", Color(0.86, 0.82, 0.42))
			pct.add_theme_font_size_override("font_size", 10)
			box.add_child(pct)
		row.add_child(box)
	for fluid in fluids:
		var label := Label.new()
		label.text = "%s\n%d mB" % [str(fluid.get("fluid_name", "?")), int(fluid.get("amount", 0))]
		label.custom_minimum_size = Vector2(98, SLOT_SIZE)
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		label.add_theme_color_override("font_color", Color(0.55, 0.75, 1.0))
		row.add_child(label)


func _source_tag(ref: NEIIndex.RecipeRef) -> String:
	if ref.recipe_type == "crafting":
		return "Craft: hand" if ref.machine_type.is_empty() else "Craft: %s" % ref.machine_type
	if ref.recipe_type == "machine":
		return "Machine: %s  T%d  %d EU/t  %d ticks" % [ref.machine_type, ref.min_tier, ref.eu_per_tick, ref.duration_ticks]
	return ref.recipe_type


func _try_creative_pick(item_id: int, button: int) -> bool:
	if button != MOUSE_BUTTON_MIDDLE and not Input.is_key_pressed(KEY_SHIFT):
		return false
	if player == null or player.inventory == null:
		return false
	if player.game_mode != PlayerController.GameMode.CREATIVE:
		return false
	var stack := ItemStack.new(item_id, 1)
	var count := stack.get_max_stack()
	player.inventory.add_item(item_id, count)
	player.inventory_changed.emit()
	_select_item(item_id)
	_subtitle.text = "%s  |  Added x%d" % [_subtitle.text, count]
	return true


func _on_detail_slot_clicked(item_id: int, button: int) -> void:
	if _try_creative_pick(item_id, button):
		return
	_detail_mode = "recipes"
	_recipe_page = 0
	_select_item(item_id)


func _on_detail_slot_right_clicked(item_id: int) -> void:
	_detail_mode = "usage"
	_recipe_page = 0
	_select_item(item_id)


func _on_item_hovered(item_id: int) -> void:
	_hovered_item_id = item_id


func _on_item_unhovered(item_id: int) -> void:
	if _hovered_item_id == item_id:
		_hovered_item_id = 0


func _on_machine_filter_selected(index: int) -> void:
	_machine_filter = str(_machine_filter_box.get_item_metadata(index))
	_recipe_page = 0
	_show_detail()


func _on_search(query: String) -> void:
	_filtered_ids = NEIIndex.search_item_ids(query)
	_item_page = 0
	_build_grid_current_page()
	if not _filtered_ids.has(_current_item_id) and not _filtered_ids.is_empty():
		_recipe_page = 0
		_select_item(_filtered_ids[0])
	elif _filtered_ids.is_empty():
		_clear_detail()
		_title.text = "No item selected"
		_subtitle.text = "No search results"
		_icon.texture = null


func _add_section(text: String) -> void:
	var sep := HSeparator.new()
	sep.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_list.add_child(sep)
	var label := Label.new()
	label.text = text
	label.add_theme_color_override("font_color", Color(0.70, 0.84, 1.0))
	_list.add_child(label)


func _add_kv(k: String, v: String) -> void:
	var row := HBoxContainer.new()
	var key := Label.new()
	key.text = k
	key.custom_minimum_size.x = 110
	key.add_theme_color_override("font_color", Color(0.60, 0.70, 0.82))
	row.add_child(key)
	var value := Label.new()
	value.text = v
	value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	value.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	value.add_theme_color_override("font_color", Color(0.88, 0.92, 0.98))
	row.add_child(value)
	_list.add_child(row)


func _add_text(text: String, color: Color) -> void:
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.add_theme_color_override("font_color", color)
	_list.add_child(label)
