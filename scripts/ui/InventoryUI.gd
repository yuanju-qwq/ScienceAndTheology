class_name InventoryUI extends Control

const SLOT_SIZE := 36
const PADDING := 2
const COLS := 9
const ROWS := 4

var player: Node
var _is_open := false
var inventory_slots: Array[SlotUI] = []
var equip_slots: Array[SlotUI] = []


func _ready() -> void:
	visible = false
	_initialize_ui()


func _initialize_ui() -> void:
	var total_w := COLS * (SLOT_SIZE + PADDING)
	var total_h := ROWS * (SLOT_SIZE + PADDING) + 60
	size = Vector2(total_w + 160, total_h)
	position = Vector2(
		get_viewport_rect().size.x / 2 - size.x / 2,
		get_viewport_rect().size.y / 2 - size.y / 2
	)

	var bg := ColorRect.new()
	bg.size = size
	bg.color = Color(0.08, 0.08, 0.10, 0.92)
	add_child(bg)

	var title := Label.new()
	title.text = "Inventory"
	title.position = Vector2(8, 8)
	title.size = Vector2(200, 24)
	add_child(title)

	var equip_label := Label.new()
	equip_label.text = "Equipment"
	equip_label.position = Vector2(COLS * (SLOT_SIZE + PADDING) + 8, 8)
	equip_label.size = Vector2(120, 24)
	add_child(equip_label)

	var grid_container := Control.new()
	grid_container.position = Vector2(8, 36)
	grid_container.size = Vector2(COLS * (SLOT_SIZE + PADDING), ROWS * (SLOT_SIZE + PADDING))
	add_child(grid_container)

	for i in ROWS * COLS:
		var col := i % COLS
		var row := i / COLS
		var slot := SlotUI.new()
		slot.slot_index = i
		slot.position = Vector2(col * (SLOT_SIZE + PADDING), row * (SLOT_SIZE + PADDING))
		slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.clicked.connect(_on_inv_slot_clicked)
		grid_container.add_child(slot)
		inventory_slots.append(slot)

	var equip_container := Control.new()
	equip_container.position = Vector2(COLS * (SLOT_SIZE + PADDING) + 8, 36)
	equip_container.size = Vector2(140, 6 * (SLOT_SIZE + PADDING))
	add_child(equip_container)

	var equip_names := ["Main Hand", "Off Hand", "Head", "Chest", "Legs", "Feet"]
	for i in 6:
		var slot := SlotUI.new()
		slot.slot_index = i
		slot.position = Vector2(0, i * (SLOT_SIZE + PADDING))
		slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.clicked.connect(_on_equip_slot_clicked)
		equip_container.add_child(slot)
		equip_slots.append(slot)
		var label := Label.new()
		label.text = equip_names[i]
		label.position = Vector2(SLOT_SIZE + 4, i * (SLOT_SIZE + PADDING) + 8)
		label.size = Vector2(90, 20)
		equip_container.add_child(label)


func set_player(p: Node) -> void:
	player = p
	if player and player.has_signal("inventory_changed"):
		if not player.inventory_changed.is_connected(_on_player_inventory_changed):
			player.inventory_changed.connect(_on_player_inventory_changed)
	_on_player_inventory_changed()


func toggle() -> void:
	_is_open = not _is_open
	visible = _is_open
	if player and player.has_method("_set_input_locked"):
		player._set_input_locked(_is_open)
	if _is_open:
		_update_all_slots()


func _on_player_inventory_changed() -> void:
	if _is_open:
		_update_all_slots()


func _on_inv_slot_clicked(index: int, button: int) -> void:
	pass


func _on_equip_slot_clicked(index: int, button: int) -> void:
	pass


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

