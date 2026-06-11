class_name HotbarUI extends Control

signal slot_selected(index: int)

const SLOT_COUNT := 9
const SLOT_SIZE := 36
const PADDING := 2

var player: Node
var slots: Array[SlotUI] = []
var selected_index: int = 0
var _slots_initialized := false


func _ready() -> void:
	_initialize_slots()


func set_player(p: Node) -> void:
	player = p
	if player and player.has_signal("inventory_changed"):
		if not player.inventory_changed.is_connected(refresh):
			player.inventory_changed.connect(refresh)
	if player and player.has_signal("hotbar_changed"):
		if not player.hotbar_changed.is_connected(_on_player_hotbar_changed):
			player.hotbar_changed.connect(_on_player_hotbar_changed)


func _initialize_slots() -> void:
	if _slots_initialized:
		return
	for i in SLOT_COUNT:
		var slot := SlotUI.new()
		slot.slot_index = i
		slot.position = Vector2(i * (SLOT_SIZE + PADDING), 0)
		slot.size = Vector2(SLOT_SIZE, SLOT_SIZE)
		slot.clicked.connect(_on_slot_clicked)
		add_child(slot)
		slots.append(slot)
	_slots_initialized = true
	_on_player_hotbar_changed(0)


func _on_player_hotbar_changed(index: int) -> void:
	_initialize_slots()
	selected_index = index
	for i in SLOT_COUNT:
		slots[i].set_highlight(i == index)


func _on_slot_clicked(index: int, button: int) -> void:
	pass


func refresh() -> void:
	_initialize_slots()
	if player == null or player.inventory == null:
		return
	var inv = player.inventory
	for i in SLOT_COUNT:
		var data: Dictionary = inv.get_slot(i)
		var item_id := int(data.get("item_id", 0))
		var count := int(data.get("count", 0))
		if item_id > 0 and count > 0:
			slots[i].item_stack = ItemStack.new(item_id, count)
		else:
			slots[i].item_stack = null
	_on_player_hotbar_changed(selected_index)
