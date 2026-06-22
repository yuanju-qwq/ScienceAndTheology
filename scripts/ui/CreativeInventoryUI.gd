class_name CreativeInventoryUI extends Control

const SLOT_SIZE := 36
const PADDING := 2
const COLS := 9

var player: PlayerController
var _is_open := false
var _current_tab := 0
var _slots: Array[SlotUI] = []
var _tab_buttons: Array[Button] = []
var _tab_container: HBoxContainer
var _grid_container: GridContainer
var _scroll_container: ScrollContainer
var _bg: ColorRect

var _categories: Array[Dictionary] = []


func _ready() -> void:
	visible = false
	_initialize_ui()
	get_viewport().size_changed.connect(_center_in_viewport)


func _center_in_viewport() -> void:
	position = (get_viewport_rect().size - size) / 2.0


func _initialize_ui() -> void:
	_categories = [
		{ "name": tr("creative.category.materials"), "category": ItemDef.Category.MATERIALS },
		{ "name": tr("creative.category.tools"), "category": ItemDef.Category.TOOLS },
		{ "name": tr("creative.category.components"), "category": ItemDef.Category.COMPONENTS },
		{ "name": tr("creative.category.placeables"), "category": ItemDef.Category.PLACEABLES },
		{ "name": tr("creative.category.resources"), "category": ItemDef.Category.RESOURCES },
		{ "name": tr("creative.category.food"), "category": ItemDef.Category.FOOD },
		{ "name": tr("creative.category.misc"), "category": ItemDef.Category.MISC },
	]

	var panel_w := COLS * (SLOT_SIZE + PADDING) + 16
	var panel_h := 480

	size = Vector2(panel_w, panel_h)
	_center_in_viewport()

	_bg = ColorRect.new()
	_bg.size = size
	_bg.color = Color(0.08, 0.08, 0.10, 0.92)
	add_child(_bg)

	var title := Label.new()
	title.text = tr("creative.inventory.title")
	title.position = Vector2(8, 8)
	title.size = Vector2(200, 24)
	add_child(title)

	_tab_container = HBoxContainer.new()
	_tab_container.position = Vector2(8, 36)
	_tab_container.size = Vector2(panel_w - 16, 28)
	add_child(_tab_container)

	for i in _categories.size():
		var cat := _categories[i]
		var btn := Button.new()
		btn.text = cat.name
		btn.toggle_mode = true
		btn.button_pressed = (i == 0)
		btn.pressed.connect(_on_tab_pressed.bind(i))
		_tab_container.add_child(btn)
		_tab_buttons.append(btn)

	_scroll_container = ScrollContainer.new()
	_scroll_container.position = Vector2(8, 68)
	_scroll_container.size = Vector2(panel_w - 16, panel_h - 76)
	add_child(_scroll_container)

	_grid_container = GridContainer.new()
	_grid_container.columns = COLS
	_scroll_container.add_child(_grid_container)

	_current_tab = 0
	_rebuild_grid()


func _on_tab_pressed(tab_index: int) -> void:
	if _current_tab == tab_index:
		return
	_current_tab = tab_index
	for i in _tab_buttons.size():
		_tab_buttons[i].button_pressed = (i == tab_index)
	_rebuild_grid()


func _rebuild_grid() -> void:
	for slot in _slots:
		slot.queue_free()
	_slots.clear()

	var cat_data := _categories[_current_tab]
	var category := cat_data.category as int
	var item_ids := ItemDatabase.get_items_in_category(category)
	item_ids.sort()

	for item_id in item_ids:
		var slot := SlotUI.new()
		slot.slot_index = item_id
		var stack := ItemStack.new(item_id, 1)
		slot.item_stack = stack
		slot.clicked.connect(_on_slot_clicked)
		_grid_container.add_child(slot)
		_slots.append(slot)


func _on_slot_clicked(item_id: int, button: int) -> void:
	if player == null or player.inventory == null:
		return

	var def := ItemDatabase.get_item(item_id)
	if def == null:
		return

	var count := 1
	if Input.is_key_pressed(KEY_SHIFT):
		count = def.max_stack

	player.inventory.add_item(item_id, count)
	player.inventory_changed.emit()


func set_player(p: PlayerController) -> void:
	player = p


func toggle() -> void:
	_is_open = not _is_open
	visible = _is_open
