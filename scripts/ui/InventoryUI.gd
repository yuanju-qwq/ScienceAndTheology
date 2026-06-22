class_name InventoryUI extends Control

const SLOT_SIZE := 36
const PADDING := 2
const COLS := 9
const ROWS := 4

const PANEL_W := 520
const PANEL_H := 420
const TAB_H := 28

var player: PlayerController
var _is_open := false
var _current_tab := 0
var _tab_buttons: Array[Button] = []

var inventory_slots: Array[SlotUI] = []
var equip_slots: Array[SlotUI] = []

var _carried_item: ItemStack = null
var _carried_icon: TextureRect
var _carried_amount: Label

var _backpack_page: Control
var _sublimation_page: SublimationPanel
var _combat_page: CombatStatsPanel
var _content_pages: Array[Control] = []


func _ready() -> void:
	visible = false
	_initialize_ui()
	_setup_carried_visuals()
	_switch_tab(0)
	get_viewport().size_changed.connect(_center_in_viewport)


func _setup_carried_visuals() -> void:
	_carried_icon = TextureRect.new()
	_carried_icon.name = "CarriedIcon"
	_carried_icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_carried_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_carried_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_carried_icon.size = Vector2(SLOT_SIZE, SLOT_SIZE)
	_carried_icon.visible = false
	add_child(_carried_icon)

	_carried_amount = Label.new()
	_carried_amount.name = "CarriedAmount"
	_carried_amount.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_carried_amount.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_carried_amount.vertical_alignment = VERTICAL_ALIGNMENT_BOTTOM
	_carried_amount.size = Vector2(SLOT_SIZE, SLOT_SIZE)
	_carried_amount.visible = false
	add_child(_carried_amount)


func _update_carried_visual() -> void:
	if _carried_item != null and not _carried_item.is_empty():
		_carried_icon.texture = _carried_item.get_icon()
		_carried_amount.text = str(_carried_item.count) if _carried_item.count > 1 else ""
		_carried_icon.visible = true
		_carried_amount.visible = true
	else:
		_carried_item = null
		_carried_icon.visible = false
		_carried_amount.visible = false


func _process(_delta: float) -> void:
	if _carried_item != null and not _carried_item.is_empty() and _carried_icon != null:
		var mouse_pos := get_global_mouse_position()
		var half := SLOT_SIZE / 2.0
		_carried_icon.global_position = mouse_pos - Vector2(half, half)
		_carried_amount.global_position = mouse_pos - Vector2(half, half)


func _center_in_viewport() -> void:
	position = (get_viewport_rect().size - size) / 2.0


func _initialize_ui() -> void:
	size = Vector2(PANEL_W, PANEL_H)
	_center_in_viewport()

	var bg := ColorRect.new()
	bg.size = size
	bg.color = Color(0.08, 0.08, 0.10, 0.92)
	add_child(bg)

	_build_tab_bar()

	var content_y := TAB_H + 4
	var content_h := PANEL_H - content_y - 4
	var content_w := PANEL_W - 8

	# ----- Tab 0: Backpack + Equipment -----
	_backpack_page = Control.new()
	_backpack_page.position = Vector2(4, content_y)
	_backpack_page.size = Vector2(content_w, content_h)
	add_child(_backpack_page)
	_content_pages.append(_backpack_page)

	var title := Label.new()
	title.text = "Inventory"
	title.position = Vector2(4, 4)
	title.size = Vector2(200, 24)
	_backpack_page.add_child(title)

	var grid_x := 4
	var grid_y := 32
	var grid_w := COLS * (SLOT_SIZE + PADDING)
	var grid_h := ROWS * (SLOT_SIZE + PADDING)

	var grid_container := Control.new()
	grid_container.position = Vector2(grid_x, grid_y)
	grid_container.size = Vector2(grid_w, grid_h)
	_backpack_page.add_child(grid_container)

	for i in ROWS * COLS:
		var col := i % COLS
		var row := i / COLS
		var slot := SlotUI.new()
		slot.slot_index = i
		slot.position = Vector2(col * (SLOT_SIZE + PADDING), row * (SLOT_SIZE + PADDING))
		slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.clicked.connect(_on_inv_slot_clicked)
		slot.right_clicked.connect(_on_inv_slot_right_clicked)
		grid_container.add_child(slot)
		inventory_slots.append(slot)

	var equip_x := grid_x + grid_w + 16
	var equip_y := 32

	var equip_label := Label.new()
	equip_label.text = "Equipment"
	equip_label.position = Vector2(equip_x, 4)
	equip_label.size = Vector2(120, 24)
	_backpack_page.add_child(equip_label)

	var equip_container := Control.new()
	equip_container.position = Vector2(equip_x, equip_y)
	equip_container.size = Vector2(140, 6 * (SLOT_SIZE + PADDING))
	_backpack_page.add_child(equip_container)

	var equip_names := ["Main Hand", "Off Hand", "Head", "Chest", "Legs", "Feet"]
	for i in 6:
		var slot := SlotUI.new()
		slot.slot_index = i
		slot.position = Vector2(0, i * (SLOT_SIZE + PADDING))
		slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.clicked.connect(_on_equip_slot_clicked)
		slot.right_clicked.connect(_on_equip_slot_right_clicked)
		equip_container.add_child(slot)
		equip_slots.append(slot)
		var label := Label.new()
		label.text = equip_names[i]
		label.position = Vector2(SLOT_SIZE + 4, i * (SLOT_SIZE + PADDING) + 8)
		label.size = Vector2(90, 20)
		equip_container.add_child(label)

	# ----- Tab 1: Sublimation -----
	_sublimation_page = SublimationPanel.new()
	_sublimation_page.name = "SublimationPanel"
	_sublimation_page.position = Vector2(4, content_y)
	_sublimation_page.size = Vector2(content_w, content_h)
	add_child(_sublimation_page)
	_content_pages.append(_sublimation_page)

	# ----- Tab 2: Combat Stats -----
	_combat_page = CombatStatsPanel.new()
	_combat_page.name = "CombatStatsPanel"
	_combat_page.position = Vector2(4, content_y)
	_combat_page.size = Vector2(content_w, content_h)
	add_child(_combat_page)
	_content_pages.append(_combat_page)


func _build_tab_bar() -> void:
	var tab_names := ["Backpack", "Sublimation", "Combat Stats"]
	var tab_w := PANEL_W / len(tab_names)
	var bar_y := 2

	var bar := HBoxContainer.new()
	bar.position = Vector2(0, bar_y)
	bar.size = Vector2(PANEL_W, TAB_H)
	add_child(bar)

	for i in len(tab_names):
		var btn := Button.new()
		btn.text = tab_names[i]
		btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		btn.custom_minimum_size = Vector2(tab_w, TAB_H)
		btn.toggle_mode = true
		btn.pressed.connect(_on_tab_pressed.bind(i))
		bar.add_child(btn)
		_tab_buttons.append(btn)


func _on_tab_pressed(index: int) -> void:
	_switch_tab(index)


func _switch_tab(index: int) -> void:
	_current_tab = index
	for i in len(_tab_buttons):
		_tab_buttons[i].button_pressed = (i == index)
	for i in len(_content_pages):
		_content_pages[i].visible = (i == index)

	if _is_open:
		_refresh_current_tab()


func _refresh_current_tab() -> void:
	if player == null:
		return
	match _current_tab:
		0:
			_update_all_slots()
		1:
			var data = player.get_source_law_data()
			if data != null:
				_sublimation_page.setup(data)
		2:
			_combat_page.setup(player)


func set_player(p: PlayerController) -> void:
	player = p
	if player and player.has_signal("inventory_changed"):
		if not player.inventory_changed.is_connected(_on_player_inventory_changed):
			player.inventory_changed.connect(_on_player_inventory_changed)
	_on_player_inventory_changed()


func toggle() -> void:
	_is_open = not _is_open
	visible = _is_open
	if not _is_open:
		_carried_item = null
		_update_carried_visual()
	if _is_open:
		_switch_tab(_current_tab)


func _on_player_inventory_changed() -> void:
	if _is_open and _current_tab == 0:
		_update_all_slots()


func _on_inv_slot_clicked(index: int, _button: int) -> void:
	if player == null or player.inventory == null:
		return
	var inv = player.inventory
	var slot_data: Dictionary = inv.get_slot(index)
	var slot_empty := slot_data.get("empty", false) or slot_data.get("item_id", 0) == 0

	if _carried_item == null or _carried_item.is_empty():
		if not slot_empty:
			_carried_item = ItemStack.new(int(slot_data.item_id), int(slot_data.count))
			inv.set_slot(index, 0, 0)
			_update_carried_visual()
			player.inventory_changed.emit()
	else:
		if slot_empty:
			inv.set_slot(index, _carried_item.item_id, _carried_item.count)
			_carried_item = null
			_update_carried_visual()
			player.inventory_changed.emit()
		elif int(slot_data.item_id) == _carried_item.item_id:
			var max_stack := _carried_item.get_max_stack()
			var available := max_stack - int(slot_data.count)
			if available > 0:
				var to_add := mini(available, _carried_item.count)
				inv.set_slot(index, int(slot_data.item_id), int(slot_data.count) + to_add)
				_carried_item.count -= to_add
				if _carried_item.count <= 0:
					_carried_item = null
				_update_carried_visual()
				player.inventory_changed.emit()
		else:
			inv.set_slot(index, _carried_item.item_id, _carried_item.count)
			_carried_item = ItemStack.new(int(slot_data.item_id), int(slot_data.count))
			_update_carried_visual()
			player.inventory_changed.emit()


func _on_inv_slot_right_clicked(index: int) -> void:
	if player == null or player.inventory == null:
		return
	var inv = player.inventory
	var slot_data: Dictionary = inv.get_slot(index)
	var slot_empty := slot_data.get("empty", false) or slot_data.get("item_id", 0) == 0

	if _carried_item == null or _carried_item.is_empty():
		if slot_empty:
			return
		var item_id := int(slot_data.item_id)
		var count := int(slot_data.count)
		if count > 1:
			var take := count / 2
			_carried_item = ItemStack.new(item_id, take)
			inv.set_slot(index, item_id, count - take)
			_update_carried_visual()
			player.inventory_changed.emit()
		else:
			_carried_item = ItemStack.new(item_id, 1)
			inv.set_slot(index, 0, 0)
			_update_carried_visual()
			player.inventory_changed.emit()
	else:
		if slot_empty:
			inv.set_slot(index, _carried_item.item_id, 1)
			_carried_item.count -= 1
			if _carried_item.count <= 0:
				_carried_item = null
			_update_carried_visual()
			player.inventory_changed.emit()
		elif int(slot_data.item_id) == _carried_item.item_id:
			var max_stack := _carried_item.get_max_stack()
			if int(slot_data.count) < max_stack:
				inv.set_slot(index, int(slot_data.item_id), int(slot_data.count) + 1)
				_carried_item.count -= 1
				if _carried_item.count <= 0:
					_carried_item = null
				_update_carried_visual()
				player.inventory_changed.emit()


func _on_equip_slot_clicked(index: int, _button: int) -> void:
	if player == null or player.equipment == null:
		return
	var equip = player.equipment
	var equipped_id := equip.get_equipped(index)

	if _carried_item == null or _carried_item.is_empty():
		if equipped_id > 0:
			equip.unequip(index)
			_carried_item = ItemStack.new(equipped_id, 1)
			_update_carried_visual()
			_update_hand_if_main_hand(index)
			player.inventory_changed.emit()
	else:
		var def := _carried_item.get_item_def()
		var can_equip := index == GDPlayerEquipment.SLOT_MAIN_HAND or (def != null and def.tool_stats != null)
		if not can_equip:
			return
		if equipped_id > 0:
			if _carried_item.item_id == equipped_id:
				return
			equip.unequip(index)
			equip.equip(index, _carried_item.item_id)
			_carried_item = ItemStack.new(equipped_id, 1)
			_update_carried_visual()
			_update_hand_if_main_hand(index)
			player.inventory_changed.emit()
		else:
			equip.equip(index, _carried_item.item_id)
			_carried_item.count -= 1
			if _carried_item.count <= 0:
				_carried_item = null
			_update_carried_visual()
			_update_hand_if_main_hand(index)
			player.inventory_changed.emit()


func _on_equip_slot_right_clicked(index: int) -> void:
	if player == null or player.equipment == null or player.inventory == null:
		return
	var equipped_id := player.equipment.get_equipped(index)
	if equipped_id <= 0:
		return
	player.equipment.unequip(index)
	if _carried_item == null or _carried_item.is_empty():
		_carried_item = ItemStack.new(equipped_id, 1)
		_update_carried_visual()
	else:
		player.inventory.add_item(equipped_id, 1)
	_update_hand_if_main_hand(index)
	player.inventory_changed.emit()


func _update_hand_if_main_hand(equip_index: int) -> void:
	if equip_index == GDPlayerEquipment.SLOT_MAIN_HAND and player.hand:
		var item_id := player.equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)
		player.hand.set_item(item_id)
		player.hotbar_changed.emit(0)


func _update_all_slots() -> void:
	if player == null or player.inventory == null:
		return
	var inv = player.inventory
	for i in inventory_slots.size():
		var data: Dictionary = inv.get_slot(i)
		var item_id := int(data.get("item_id", 0))
		var count := int(data.get("count", 0))
		if item_id > 0 and count > 0:
			inventory_slots[i].item_stack = ItemStack.new(item_id, count)
		else:
			inventory_slots[i].item_stack = null

	var equip = player.equipment
	if equip == null:
		return
	for i in equip_slots.size():
		var item_id: int = equip.get_equipped(i)
		if item_id > 0:
			equip_slots[i].item_stack = ItemStack.new(item_id, 1)
		else:
			equip_slots[i].item_stack = null