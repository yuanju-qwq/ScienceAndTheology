class_name NeiPanel extends Control

const SLOT_SIZE := 36
const PADDING := 2
const GRID_COLS := 8

var _is_open := false
var _current_item_id := 0
var _detail_mode := "recipes"

var _item_ids: Array[int] = []
var _filtered_ids: Array[int] = []
var _slots: Array[SlotUI] = []

var _bg: ColorRect
var _search_box: LineEdit
var _grid_scroll: ScrollContainer
var _grid_container: GridContainer
var _detail_panel: Panel
var _detail_icon: TextureRect
var _detail_title: Label
var _detail_tab_container: HBoxContainer
var _detail_scroll: ScrollContainer
var _detail_list: VBoxContainer
var _close_btn: Button


func _ready() -> void:
	visible = false
	_build_ui()
	get_viewport().size_changed.connect(_center_in_viewport)


func _center_in_viewport() -> void:
	position = (get_viewport_rect().size - size) / 2.0


func toggle() -> void:
	_is_open = not _is_open
	visible = _is_open
	if _is_open:
		_refresh()


func open_for_item(item_id: int) -> void:
	if not _is_open:
		_is_open = true
		visible = true
	_refresh_grid()
	_select_item(item_id)
	_search_box.clear()


func _build_ui() -> void:
	size = Vector2(900, 600)
	_center_in_viewport()

	_bg = ColorRect.new()
	_bg.size = size
	_bg.color = Color(0.07, 0.07, 0.09, 0.95)
	add_child(_bg)

	var title := Label.new()
	title.text = tr("nei.title")
	title.position = Vector2(8, 4)
	title.size = Vector2(200, 22)
	title.add_theme_color_override("font_color", Color(0.8, 0.9, 1.0))
	add_child(title)

	_close_btn = Button.new()
	_close_btn.text = "X"
	_close_btn.position = Vector2(size.x - 28, 2)
	_close_btn.size = Vector2(24, 24)
	_close_btn.pressed.connect(_close)
	add_child(_close_btn)

	var sep := HSeparator.new()
	sep.position = Vector2(0, 26)
	sep.size = Vector2(size.x, 4)
	add_child(sep)

	_build_left_panel()
	_build_detail_panel()


func _build_left_panel() -> void:
	var left_w := 310

	_search_box = LineEdit.new()
	_search_box.position = Vector2(8, 32)
	_search_box.size = Vector2(left_w - 8, 22)
	_search_box.placeholder_text = tr("nei.search_hint")
	_search_box.text_changed.connect(_on_search)
	add_child(_search_box)

	var grid_y := 58
	_grid_scroll = ScrollContainer.new()
	_grid_scroll.position = Vector2(8, grid_y)
	_grid_scroll.size = Vector2(left_w - 8, size.y - grid_y - 8)
	add_child(_grid_scroll)

	_grid_container = GridContainer.new()
	_grid_container.columns = GRID_COLS
	_grid_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_grid_scroll.add_child(_grid_container)


func _build_detail_panel() -> void:
	var left_w := 310
	var dx := left_w + 12
	var dw := size.x - dx - 8

	_detail_panel = Panel.new()
	_detail_panel.position = Vector2(dx, 32)
	_detail_panel.size = Vector2(dw, size.y - 38)
	add_child(_detail_panel)

	_detail_icon = TextureRect.new()
	_detail_icon.position = Vector2(8, 8)
	_detail_icon.size = Vector2(40, 40)
	_detail_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_detail_icon.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	_detail_panel.add_child(_detail_icon)

	_detail_title = Label.new()
	_detail_title.position = Vector2(54, 8)
	_detail_title.size = Vector2(dw - 60, 22)
	_detail_title.add_theme_color_override("font_color", Color(0.9, 0.95, 1.0))
	_detail_title.add_theme_font_size_override("font_size", 18)
	_detail_panel.add_child(_detail_title)

	_detail_tab_container = HBoxContainer.new()
	_detail_tab_container.position = Vector2(8, 52)
	_detail_tab_container.size = Vector2(dw - 16, 24)
	_detail_panel.add_child(_detail_tab_container)

	_detail_scroll = ScrollContainer.new()
	_detail_scroll.position = Vector2(4, 80)
	_detail_scroll.size = Vector2(dw - 8, _detail_panel.size.y - 84)
	_detail_panel.add_child(_detail_scroll)

	_detail_list = VBoxContainer.new()
	_detail_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_scroll.add_child(_detail_list)


func _refresh() -> void:
	RecipeGraph.rebuild()
	_refresh_grid()


func _refresh_grid() -> void:
	for slot in _slots:
		slot.queue_free()
	_slots.clear()

	_item_ids = RecipeGraph.find_item_ids("")
	_filtered_ids = _item_ids.duplicate()
	_filtered_ids.sort()
	_build_grid_items(_filtered_ids)


func _build_grid_items(item_ids: Array[int]) -> void:
	for child in _grid_container.get_children():
		_grid_container.remove_child(child)
		child.queue_free()
	_slots.clear()

	for item_id in item_ids:
		var slot := SlotUI.new()
		slot.slot_index = item_id
		slot.custom_minimum_size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
		var stack := ItemStack.new(item_id, 1)
		slot.item_stack = stack
		slot.clicked.connect(_on_slot_clicked)
		_grid_container.add_child(slot)
		_slots.append(slot)


func _select_item(item_id: int) -> void:
	if item_id <= 0:
		_detail_title.text = ""
		_detail_icon.texture = null
		_clear_detail()
		return

	_current_item_id = item_id

	var def := ItemDatabase.get_item(item_id)
	if def:
		_detail_title.text = tr(def.title_key)
		_detail_icon.texture = def.icon
	else:
		_detail_title.text = "Item #%d" % item_id
		_detail_icon.texture = null

	_rebuild_detail_tabs()
	_show_detail(_detail_mode)


func _on_slot_clicked(item_id: int, _button: int) -> void:
	_select_item(item_id)


func _clear_detail() -> void:
	for child in _detail_list.get_children():
		child.queue_free()
	for child in _detail_tab_container.get_children():
		child.queue_free()


func _rebuild_detail_tabs() -> void:
	for child in _detail_tab_container.get_children():
		child.queue_free()

	_detail_mode = "recipes"
	var recipes_btn := Button.new()
	recipes_btn.text = tr("nei.tab_recipes")
	recipes_btn.toggle_mode = true
	recipes_btn.button_pressed = true
	recipes_btn.pressed.connect(_on_detail_tab.bind("recipes"))
	_detail_tab_container.add_child(recipes_btn)

	var usage_btn := Button.new()
	usage_btn.text = tr("nei.tab_usage")
	usage_btn.toggle_mode = true
	usage_btn.pressed.connect(_on_detail_tab.bind("usage"))
	_detail_tab_container.add_child(usage_btn)


func _on_detail_tab(mode: String) -> void:
	_detail_mode = mode
	_show_detail(mode)


func _show_detail(mode: String) -> void:
	_clear_detail()

	var refs: Array[RecipeGraph.RecipeRef]
	if mode == "recipes":
		refs = RecipeGraph.get_recipes_for_output(_current_item_id)
	else:
		refs = RecipeGraph.get_recipes_for_input(_current_item_id)

	if refs.is_empty():
		var label := Label.new()
		label.text = tr("nei.no_recipes")
		label.modulate = Color(0.6, 0.6, 0.7)
		_detail_list.add_child(label)
		return

	for ref in refs:
		var card := _build_recipe_card(ref)
		_detail_list.add_child(card)
		var sep := HSeparator.new()
		sep.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		_detail_list.add_child(sep)


func _build_recipe_card(ref: RecipeGraph.RecipeRef) -> Control:
	var card := Panel.new()
	card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	card.custom_minimum_size.y = 64

	var vbox := VBoxContainer.new()
	vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.position = Vector2(4, 2)
	card.add_child(vbox)

	var header := HBoxContainer.new()
	header.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(header)

	var source_label := Label.new()
	source_label.text = _source_tag(ref)
	source_label.modulate = Color(0.5, 0.8, 1.0)
	source_label.add_theme_font_size_override("font_size", 12)
	header.add_child(source_label)

	if ref.recipe_type == "machine":
		var data := ref.data
		var tier := int(data.get("min_tier", 0))
		var eu := int(data.get("eu_per_tick", 0))
		var dur := int(data.get("duration_ticks", 0))
		var info := Label.new()
		info.text = "  T%d  %d EU/t  %d ticks" % [tier, eu, dur]
		info.modulate = Color(0.8, 0.8, 0.6)
		info.add_theme_font_size_override("font_size", 12)
		header.add_child(info)

	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(row)

	_add_icon_row(row, ref.inputs, false)
	_build_arrow_label(row)
	_add_icon_row(row, ref.outputs, true)

	return card


func _add_icon_row(parent: HBoxContainer, items: Array[Dictionary], is_output: bool) -> void:
	for entry in items:
		var iid := int(entry.get("item_id", 0))
		var count := int(entry.get("count", entry.get("amount", 1)))
		if iid <= 0:
			continue

		var container := VBoxContainer.new()
		container.alignment = BOX_ALIGNMENT_CENTER

		var slot := SlotUI.new()
		slot.custom_minimum_size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.slot_index = iid
		slot.item_stack = ItemStack.new(iid, count)
		slot.clicked.connect(_on_detail_slot_clicked)
		container.add_child(slot)

		if is_output:
			var prob := float(entry.get("probability", 1.0))
			if prob < 1.0:
				var pct := Label.new()
				pct.text = "%d%%" % int(prob * 100)
				pct.modulate = Color(0.8, 0.8, 0.4)
				pct.add_theme_font_size_override("font_size", 10)
				pct.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
				container.add_child(pct)

		parent.add_child(container)


func _build_arrow_label(parent: HBoxContainer) -> void:
	var label := Label.new()
	label.text = "  →  "
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.custom_minimum_size.y = SLOT_SIZE
	parent.add_child(label)


func _source_tag(ref: RecipeGraph.RecipeRef) -> String:
	match ref.recipe_type:
		"crafting":
			var station := ref.data.get("required_station", "")
			if station == "":
				return "Craft (Hand)"
			return "Craft (%s)" % station
		"machine":
			return "Machine: %s" % ref.data.get("machine_type", "?")
		_:
			return "?"


func _on_detail_slot_clicked(item_id: int, _button: int) -> void:
	_select_item(item_id)


func _on_search(query: String) -> void:
	var q := query.strip_edges()
	if q.is_empty():
		_filtered_ids = _item_ids.duplicate()
	else:
		_filtered_ids = RecipeGraph.find_item_ids(q)
	_filtered_ids.sort()
	_build_grid_items(_filtered_ids)


func _close() -> void:
	_is_open = false
	visible = false
