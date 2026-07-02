class_name NEISidebar
extends Control

## NEISidebar is the always-on item panel inspired by codechicken.nei LayoutManager.
## Shows a scrollable item grid, search bar, mode toggle and bookmarks row.
## In CHEAT mode, clicking items gives them to the player.
## In RECIPE mode, R/U/M keys on hovered items open the recipe browser.

signal item_activated(item_id: int, mode: String)
signal item_cheat_requested(item_id: int, count: int)

const SLOT_SIZE := 32
const GRID_COLS := 4
const ITEMS_PER_PAGE := GRID_COLS * 6  # 24 items per page
const SIDEBAR_W := 180
const BOOKMARK_ROWS := 2

var player: PlayerController = null
var _is_visible := false
var _filtered_ids: Array[int] = []
var _page := 0
var _hovered_item_id := 0
var _search_query := ""

var _bg: ColorRect
var _bookmark_grid: GridContainer
var _item_grid: GridContainer
var _item_scroll: ScrollContainer
var _search_box: LineEdit
var _mode_btn: Button
var _prev_btn: Button
var _next_btn: Button
var _page_label: Label
var _subset_btn: Button
var _sidebar_side: int = NEISettings.SidebarSide.RIGHT


func _ready() -> void:
	visible = false
	mouse_filter = Control.MOUSE_FILTER_STOP
	_build_ui()
	_apply_side()
	if NEISettings != null:
		NEISettings.mode_changed.connect(_on_mode_changed)
		NEISettings.bookmarks_changed.connect(_refresh_bookmarks)
		NEISettings.settings_changed.connect(_on_settings_changed)
	_on_mode_changed(NEISettings.mode if NEISettings != null else NEISettings.Mode.RECIPE)


func set_player(p: PlayerController) -> void:
	player = p


func show_sidebar() -> void:
	_is_visible = true
	visible = true
	_refresh()


func hide_sidebar() -> void:
	_is_visible = false
	visible = false


func toggle_sidebar() -> void:
	if _is_visible:
		hide_sidebar()
	else:
		show_sidebar()


# Reposition the sidebar based on the configured side (left/right).
func _apply_side() -> void:
	var viewport_size := get_viewport_rect().size
	size = Vector2(SIDEBAR_W, viewport_size.y)
	if _sidebar_side == NEISettings.SidebarSide.RIGHT:
		position = Vector2(viewport_size.x - SIDEBAR_W, 0)
	else:
		position = Vector2(0, 0)


func _build_ui() -> void:
	_apply_side()
	get_viewport().size_changed.connect(_apply_side)

	_bg = ColorRect.new()
	_bg.size = size
	_bg.color = Color(0.055, 0.058, 0.07, 0.92)
	_bg.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(_bg)

	# Mode toggle button at the top.
	_mode_btn = Button.new()
	_mode_btn.text = tr("nei.sidebar.mode_recipe")
	_mode_btn.position = Vector2(4, 4)
	_mode_btn.size = Vector2(SIDEBAR_W - 8, 24)
	_mode_btn.pressed.connect(_on_mode_pressed)
	add_child(_mode_btn)

	# Bookmarks row — pinned items for quick access.
	var bookmark_label := Label.new()
	bookmark_label.text = tr("nei.sidebar.bookmarks")
	bookmark_label.position = Vector2(4, 32)
	bookmark_label.size = Vector2(SIDEBAR_W - 8, 16)
	bookmark_label.add_theme_color_override("font_color", Color(0.62, 0.70, 0.82))
	bookmark_label.add_theme_font_size_override("font_size", 10)
	add_child(bookmark_label)

	var bookmark_scroll := ScrollContainer.new()
	bookmark_scroll.position = Vector2(4, 48)
	bookmark_scroll.size = Vector2(SIDEBAR_W - 8, BOOKMARK_ROWS * SLOT_SIZE + 4)
	bookmark_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	bookmark_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	add_child(bookmark_scroll)

	_bookmark_grid = GridContainer.new()
	_bookmark_grid.columns = GRID_COLS
	bookmark_scroll.add_child(_bookmark_grid)

	# Item grid — main scrollable item list.
	_item_scroll = ScrollContainer.new()
	_item_scroll.position = Vector2(4, 48 + BOOKMARK_ROWS * SLOT_SIZE + 8)
	_item_scroll.size = Vector2(SIDEBAR_W - 8, size.y - 48 - BOOKMARK_ROWS * SLOT_SIZE - 8 - 60)
	_item_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	add_child(_item_scroll)

	_item_grid = GridContainer.new()
	_item_grid.columns = GRID_COLS
	_item_scroll.add_child(_item_grid)

	# Page navigation.
	_prev_btn = Button.new()
	_prev_btn.text = "<"
	_prev_btn.position = Vector2(4, size.y - 56)
	_prev_btn.size = Vector2(28, 24)
	_prev_btn.pressed.connect(_on_page_delta.bind(-1))
	add_child(_prev_btn)

	_page_label = Label.new()
	_page_label.position = Vector2(36, size.y - 56)
	_page_label.size = Vector2(SIDEBAR_W - 72, 24)
	_page_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_page_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_page_label.add_theme_color_override("font_color", Color(0.82, 0.90, 1.0))
	add_child(_page_label)

	_next_btn = Button.new()
	_next_btn.text = ">"
	_next_btn.position = Vector2(SIDEBAR_W - 32, size.y - 56)
	_next_btn.size = Vector2(28, 24)
	_next_btn.pressed.connect(_on_page_delta.bind(1))
	add_child(_next_btn)

	# Search box at the bottom.
	_search_box = LineEdit.new()
	_search_box.position = Vector2(4, size.y - 28)
	_search_box.size = Vector2(SIDEBAR_W - 8, 24)
	_search_box.placeholder_text = tr("nei.search_hint")
	_search_box.text_changed.connect(_on_search)
	add_child(_search_box)


func _refresh() -> void:
	NEIIndex.ensure_built()
	_filtered_ids = NEIIndex.search_item_ids(_search_query)
	_page = 0
	_build_grid_current_page()
	_refresh_bookmarks()


func _refresh_bookmarks() -> void:
	if _bookmark_grid == null:
		return
	for child in _bookmark_grid.get_children():
		child.queue_free()
	if NEISettings == null:
		return
	for item_id in NEISettings.bookmarks:
		_bookmark_grid.add_child(_make_item_slot(item_id, 1, true))


func _build_grid_current_page() -> void:
	for child in _item_grid.get_children():
		child.queue_free()
	var total_pages := _total_pages()
	_page = clampi(_page, 0, total_pages - 1)
	var first := _page * ITEMS_PER_PAGE
	var last := mini(first + ITEMS_PER_PAGE, _filtered_ids.size())
	for i in range(first, last):
		_item_grid.add_child(_make_item_slot(_filtered_ids[i], 1, false))
	_update_pager()


func _make_item_slot(item_id: int, count: int, is_bookmark: bool) -> SlotUI:
	var slot := SlotUI.new()
	slot.slot_index = item_id
	slot.custom_minimum_size = Vector2(SLOT_SIZE, SLOT_SIZE)
	slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
	slot.item_stack = ItemStack.new(item_id, count)
	slot.tooltip_text = _build_item_tooltip(item_id)
	slot.clicked.connect(_on_slot_clicked.bind(item_id))
	slot.right_clicked.connect(_on_slot_right_clicked.bind(item_id))
	slot.mouse_entered.connect(_on_item_hovered.bind(item_id))
	slot.mouse_exited.connect(_on_item_unhovered.bind(item_id))
	return slot


# Build a multi-line tooltip with NEI metadata for an item.
# Shows recipe/usage counts, ore dict, source, and key bindings.
func _build_item_tooltip(item_id: int) -> String:
	var def = ItemDatabase.get_item(item_id)
	if def == null:
		return tr("nei.info.unregistered_item_format") % item_id
	var lines := PackedStringArray()
	lines.append(tr(def.title_key))
	var key := NEIIndex.get_item_key(item_id)
	if not key.is_empty():
		lines.append(tr("nei.sidebar.key_format") % key)
	var source := NEIIndex.get_item_source(item_id)
	if not source.is_empty():
		lines.append(tr("nei.sidebar.source_format") % source)
	var ores := NEIIndex.get_ores_for_item(item_id)
	if not ores.is_empty():
		lines.append(tr("nei.sidebar.ore_format") % ", ".join(ores))
	var recipe_count := NEIIndex.get_recipes_for_output(item_id).size()
	var usage_count := NEIIndex.get_recipes_for_input(item_id).size()
	lines.append(tr("nei.sidebar.recipe_usage_format") % [recipe_count, usage_count])
	lines.append(tr("nei.tooltip.hint"))
	return "\n".join(lines)


func _total_pages() -> int:
	if _filtered_ids.is_empty():
		return 1
	return maxi(1, int(ceil(float(_filtered_ids.size()) / float(ITEMS_PER_PAGE))))


func _update_pager() -> void:
	var total_pages := _total_pages()
	_prev_btn.disabled = _page <= 0
	_next_btn.disabled = _page >= total_pages - 1
	_page_label.text = tr("nei.sidebar.page_format") % [_page + 1, total_pages]


func _on_page_delta(delta: int) -> void:
	_page = clampi(_page + delta, 0, _total_pages() - 1)
	_build_grid_current_page()


func _on_search(query: String) -> void:
	_search_query = query
	_filtered_ids = NEIIndex.search_item_ids(query)
	_page = 0
	_build_grid_current_page()


func _on_mode_pressed() -> void:
	if NEISettings != null:
		NEISettings.cycle_mode()


func _on_mode_changed(new_mode: int) -> void:
	match new_mode:
		NEISettings.Mode.RECIPE: _mode_btn.text = tr("nei.sidebar.mode_recipe")
		NEISettings.Mode.CHEAT: _mode_btn.text = tr("nei.sidebar.mode_cheat")
		NEISettings.Mode.UTILITY: _mode_btn.text = tr("nei.sidebar.mode_utility")


func _on_settings_changed() -> void:
	if _sidebar_side != NEISettings.sidebar_side:
		_sidebar_side = NEISettings.sidebar_side
		_apply_side()


func _on_slot_clicked(item_id: int, button: int) -> void:
	if NEISettings != null and NEISettings.mode == NEISettings.Mode.CHEAT:
		var count := 1
		if NEISettings.cheat_gives_full_stack:
			var stack := ItemStack.new(item_id, 1)
			count = stack.get_max_stack()
		if button == MOUSE_BUTTON_RIGHT:
			count = 1
		item_cheat_requested.emit(item_id, count)
		_try_give_item(item_id, count)
		return
	# Recipe mode: left click opens recipes.
	item_activated.emit(item_id, "recipes")


func _on_slot_right_clicked(item_id: int) -> void:
	if NEISettings != null and NEISettings.mode == NEISettings.Mode.CHEAT:
		_try_give_item(item_id, 1)
		return
	# Recipe mode: right click opens usages.
	item_activated.emit(item_id, "usage")


# Give an item to the player (cheat mode). Only works in CREATIVE.
func _try_give_item(item_id: int, count: int) -> void:
	if player == null or player.inventory == null:
		return
	if player.game_mode != PlayerController.GameMode.CREATIVE:
		return
	player.inventory.add_item(item_id, count)
	player.inventory_changed.emit()


func _on_item_hovered(item_id: int) -> void:
	_hovered_item_id = item_id


func _on_item_unhovered(item_id: int) -> void:
	if _hovered_item_id == item_id:
		_hovered_item_id = 0


# Handle R/U/M/B hotkeys on hovered items — mirrors NEI key bindings.
func _input(event: InputEvent) -> void:
	if not _is_visible or not visible:
		return
	if not (event is InputEventKey):
		return
	var key_event := event as InputEventKey
	if not key_event.pressed or key_event.echo:
		return
	if _hovered_item_id <= 0:
		return
	# Only handle when search box is not focused.
	if _search_box.has_focus():
		return
	match key_event.keycode:
		KEY_R:
			item_activated.emit(_hovered_item_id, "recipes")
			get_viewport().set_input_as_handled()
		KEY_U:
			item_activated.emit(_hovered_item_id, "usage")
			get_viewport().set_input_as_handled()
		KEY_M:
			item_activated.emit(_hovered_item_id, "machines")
			get_viewport().set_input_as_handled()
		KEY_B:
			if NEISettings != null:
				NEISettings.toggle_bookmark(_hovered_item_id)
				get_viewport().set_input_as_handled()


# Set the search query programmatically (e.g. from search history).
func set_search(query: String) -> void:
	_search_box.text = query
	_on_search(query)


# Get the currently hovered item id (for external tooltip display).
func get_hovered_item_id() -> int:
	return _hovered_item_id
