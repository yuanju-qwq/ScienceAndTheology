class_name CraftingUI extends Control

var player: PlayerController
var _station: String = ""
var _recipes: Array = []
var _current_category: String = ""
var _is_open := false
var _rebuilding := false

signal crafted(item_id: int, count: int)

# UI nodes (built procedurally)
var _bg: ColorRect
var _title: Label
var _station_label: Label
var _tab_container: HBoxContainer
var _recipe_list: VBoxContainer
var _scroll: ScrollContainer

const CATEGORY_ORDER: Array = [
	"materials", "tools", "parts", "wires", "cables", "circuits", "machines", "misc",
]
const CATEGORY_LABELS: Dictionary = {
	materials = "crafting.category.materials",
	tools = "crafting.category.tools",
	parts = "crafting.category.parts",
	wires = "crafting.category.wires",
	cables = "crafting.category.cables",
	circuits = "crafting.category.circuits",
	machines = "crafting.category.machines",
	misc = "crafting.category.misc"
}


func _ready() -> void:
	visible = false
	_build_ui()
	get_viewport().size_changed.connect(_center_in_viewport)


func _center_in_viewport() -> void:
	position = (get_viewport_rect().size - size) / 2.0


func _build_ui() -> void:
	var panel_w := 480
	var panel_h := 400

	size = Vector2(panel_w, panel_h)
	_center_in_viewport()

	_bg = ColorRect.new()
	_bg.size = size
	_bg.color = Color(0.08, 0.08, 0.10, 0.92)
	add_child(_bg)

	_title = Label.new()
	_title.text = tr("crafting.title")
	_title.position = Vector2(8, 4)
	_title.size = Vector2(200, 20)
	add_child(_title)

	_station_label = Label.new()
	_station_label.text = ""
	_station_label.position = Vector2(220, 4)
	_station_label.size = Vector2(250, 20)
	_station_label.modulate = Color(0.6, 0.8, 1.0)
	add_child(_station_label)

	_tab_container = HBoxContainer.new()
	_tab_container.position = Vector2(8, 28)
	_tab_container.size = Vector2(panel_w - 16, 24)
	add_child(_tab_container)

	_scroll = ScrollContainer.new()
	_scroll.position = Vector2(8, 56)
	_scroll.size = Vector2(panel_w - 16, panel_h - 64)
	add_child(_scroll)

	_recipe_list = VBoxContainer.new()
	_recipe_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_scroll.add_child(_recipe_list)


func toggle() -> void:
	_is_open = not _is_open
	visible = _is_open
	if _is_open:
		_refresh()


func set_player(p: PlayerController) -> void:
	player = p


func set_station(station: String) -> void:
	_station = station
	_station_label.text = " - %s" % _station_display_name(station)


func _station_display_name(station: String) -> String:
	match station:
		"workbench":
			return tr("station.workbench")
		_:
			return tr("station.hand_crafting")


func _refresh() -> void:
	_refresh_recipes()
	_build_tabs()
	# Select first tab if none selected
	if _current_category.is_empty() and _recipes.size() > 0:
		var first_recipe: Dictionary = _recipes[0]
		_select_category(first_recipe.get("category", "") as String)
	else:
		_select_category(_current_category)


func _refresh_recipes() -> void:
	_recipes = GDCraftingManager.get_recipes_for_station(_station)


func _build_tabs() -> void:
	_rebuilding = true
	for child in _tab_container.get_children():
		child.queue_free()

	var seen_categories: Dictionary = {}
	for recipe: Dictionary in _recipes:
		var cat: String = recipe.get("category", "")
		if not cat.is_empty() and not seen_categories.has(cat):
			seen_categories[cat] = true

	for cat: String in CATEGORY_ORDER:
		if not seen_categories.has(cat):
			continue
		var label_key: String = CATEGORY_LABELS.get(cat, cat)
		var btn := Button.new()
		btn.text = tr(label_key)
		btn.toggle_mode = true
		btn.button_pressed = (cat == _current_category)
		if cat == _current_category:
			btn.disabled = true
		btn.pressed.connect(_on_tab_pressed.bind(cat))
		_tab_container.add_child(btn)
	_rebuilding = false


func _on_tab_pressed(category: String) -> void:
	_select_category(category)


@warning_ignore("unsafe_call_argument")
func _select_category(category: String) -> void:
	_current_category = category
	_build_tabs()

	# Populate recipe list
	for child in _recipe_list.get_children():
		child.queue_free()

	var inventory_valid := player != null and player.inventory != null
	var equipment_valid := player != null and player.equipment != null

	for recipe: Dictionary in _recipes:
		if recipe.get("category", "") != category:
			continue

		var _recipe_name: String = recipe.get("name", "")
		var out_id := int(recipe.get("output_item_id", 0))
		var out_count := int(recipe.get("output_count", 0))
		var inputs: Array = recipe.get("inputs", [])
		var tool: String = recipe.get("required_tool", "")

		if out_id <= 0:
			continue

		var can_craft := inventory_valid and equipment_valid

		# Build recipe row
		var row := HBoxContainer.new()
		row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		_recipe_list.add_child(row)

		# Output icon + name
		var out_box := VBoxContainer.new()
		out_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		out_box.size_flags_stretch_ratio = 0.3
		row.add_child(out_box)

		var out_name := tr(GDCraftingManager.get_item_title_key(out_id))
		var out_label := Label.new()
		out_label.text = tr("crafting.output_format") % [out_name, out_count]
		out_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		out_box.add_child(out_label)

		# Inputs
		var input_box := VBoxContainer.new()
		input_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		input_box.size_flags_stretch_ratio = 0.5
		row.add_child(input_box)

		for input: Dictionary in inputs:
			var in_id := int(input.get("item_id", 0))
			var in_count := int(input.get("count", 0))
			var have_count := 0
			if inventory_valid:
				have_count = player.inventory.count_item(in_id)
			var in_name := tr(GDCraftingManager.get_item_title_key(in_id))
			var color := Color(0.4, 1.0, 0.4) if have_count >= in_count else Color(1.0, 0.4, 0.4)
			var in_label := Label.new()
			in_label.text = tr("crafting.input_format") % [in_name, in_count, have_count]
			in_label.modulate = color
			input_box.add_child(in_label)

			if have_count < in_count:
				can_craft = false

		# Tool requirement
		if tool != null and not tool.is_empty():
			var has_tool := false
			if inventory_valid and equipment_valid:
				has_tool = _player_has_tool(tool)
			var t_label := Label.new()
			var tick := "✓" if has_tool else "✗"
			t_label.text = tr("crafting.tool_format") % [tool, tick]
			t_label.modulate = Color(0.4, 1.0, 0.4) if has_tool else Color(1.0, 0.4, 0.4)
			input_box.add_child(t_label)
			if not has_tool:
				can_craft = false

		# Craft button
		var craft_btn := Button.new()
		craft_btn.text = tr("crafting.craft_button")
		craft_btn.disabled = not can_craft
		craft_btn.pressed.connect(_on_craft_pressed.bind(recipe))
		row.add_child(craft_btn)

		# Separator
		var sep := HSeparator.new()
		_recipe_list.add_child(sep)


@warning_ignore("unsafe_call_argument")
func _player_has_tool(tool_name: String) -> bool:
	if tool_name.is_empty():
		return true
	var tool_lower := tool_name.to_lower()

	var inv: GDPlayerInventory = player.inventory
	var eq: GDPlayerEquipment = player.equipment

	# Scan full inventory (36 slots)
	for i in range(inv.get_slot_count()):
		var slot: Dictionary = inv.get_slot(i)
		var id := int(slot.get("item_id", 0))
		if id > 0:
			var item_name := GDCraftingManager.get_item_title_key(id)
			if item_name.length() > 0 and item_name.to_lower().contains(tool_lower):
				return true

	# Scan equipment (6 slots)
	for slot_idx in range(6):
		var id: int = eq.get_equipped(slot_idx)
		if id > 0:
			var item_name := GDCraftingManager.get_item_title_key(id)
			if item_name.length() > 0 and item_name.to_lower().contains(tool_lower):
				return true

	return false


@warning_ignore("unsafe_call_argument")
func _on_craft_pressed(recipe: Dictionary) -> void:
	if player == null:
		return

	var out_id := int(recipe.get("output_item_id", 0))
	var out_count := int(recipe.get("output_count", 0))
	var command_server: GameCommandServer = player.get_command_server()
	if command_server == null:
		return

	var result: Dictionary = command_server.submit_command({
		"type": GameCommandServer.COMMAND_CRAFT_RECIPE,
		"recipe": recipe,
		"station": _station,
		"dimension": player.get_current_dimension(),
		"cell": player.get_current_cell(),
	})
	if not bool(result.get("ok", false)):
		return

	crafted.emit(out_id, int(result.get("crafted_count", out_count)))

	# Refresh UI to update counts
	_select_category(_current_category)
