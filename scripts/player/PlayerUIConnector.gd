# PlayerUIConnector — Manages all UI toggle and connection logic for the player.
# Extracted from PlayerController to separate UI wiring from gameplay.
class_name PlayerUIConnector
extends Node

var _player: PlayerController = null


func setup(player: PlayerController) -> void:
	_player = player


func connect_ui() -> void:
	var hotbar_ui: HotbarUI = _player.hotbar_ui
	var inventory_ui: InventoryUI = _player.inventory_ui
	var crafting_ui: CraftingUI = _player.crafting_ui
	var furnace_ui: FurnaceUI = _player.furnace_ui
	var knapping_ui: KnappingUI = _player.knapping_ui

	if hotbar_ui:
		hotbar_ui.set_player(_player)
	if inventory_ui:
		inventory_ui.set_player(_player)
	if crafting_ui:
		crafting_ui.set_player(_player)
	if furnace_ui:
		furnace_ui.set_player(_player)
		if not furnace_ui.closed.is_connected(_on_furnace_ui_closed):
			furnace_ui.closed.connect(_on_furnace_ui_closed)
	if knapping_ui:
		knapping_ui.set_player(_player)
		if not knapping_ui.closed.is_connected(_on_knapping_ui_closed):
			knapping_ui.closed.connect(_on_knapping_ui_closed)

	var creative_ui: CreativeInventoryUI = _player.creative_inventory_ui
	if creative_ui:
		creative_ui.set_player(_player)

	var nei_panel: NeiPanel = _player.nei_panel
	if nei_panel:
		nei_panel.set_player(_player)
		if not nei_panel.closed.is_connected(_on_nei_closed):
			nei_panel.closed.connect(_on_nei_closed)

	# Wire quest book UI.
	var quest_ui: QuestBookUI = _player.quest_ui
	var quest_sys := _player.quest_system
	if quest_sys and quest_ui:
		quest_ui.set_quest_system(quest_sys)
		if not quest_ui.quest_book_closed.is_connected(_on_quest_book_closed):
			quest_ui.quest_book_closed.connect(_on_quest_book_closed)


func toggle_inventory() -> void:
	var crafting_ui: CraftingUI = _player.crafting_ui
	var inventory_ui: InventoryUI = _player.inventory_ui
	if crafting_ui and crafting_ui.visible:
		toggle_crafting()
	if inventory_ui:
		inventory_ui.toggle()
		_player.set_input_locked(inventory_ui.visible)


func toggle_creative_inventory() -> void:
	var creative_ui: CreativeInventoryUI = _player.creative_inventory_ui
	if creative_ui:
		creative_ui.toggle()
		_player.set_input_locked(creative_ui._is_open)


func toggle_crafting(station: String = "") -> void:
	var crafting_ui: CraftingUI = _player.crafting_ui
	var inventory_ui: InventoryUI = _player.inventory_ui
	if crafting_ui == null:
		return
	if inventory_ui and inventory_ui.visible:
		inventory_ui.toggle()
	if station == "":
		station = _player.interaction.get_nearby_station()
	crafting_ui.set_station(station)
	crafting_ui.toggle()
	_player.set_input_locked(crafting_ui.visible)


func toggle_nei() -> void:
	var nei_panel: NeiPanel = _player.nei_panel
	if nei_panel == null:
		return
	var crafting_ui: CraftingUI = _player.crafting_ui
	var inventory_ui: InventoryUI = _player.inventory_ui
	if crafting_ui and crafting_ui.visible:
		toggle_crafting()
	if inventory_ui and inventory_ui.visible:
		inventory_ui.toggle()
	nei_panel.toggle()
	# Show the NEI sidebar alongside the panel.
	var sidebar: NEISidebar = _player.nei_sidebar
	if sidebar != null:
		if nei_panel.visible:
			sidebar.show_sidebar()
		else:
			sidebar.hide_sidebar()
	_player.set_input_locked(nei_panel.visible)


func toggle_quest_book() -> void:
	var quest_ui: QuestBookUI = _player.quest_ui
	if quest_ui == null:
		return
	var crafting_ui: CraftingUI = _player.crafting_ui
	var inventory_ui: InventoryUI = _player.inventory_ui
	if crafting_ui and crafting_ui.visible:
		toggle_crafting()
	if inventory_ui and inventory_ui.visible:
		inventory_ui.toggle()
	quest_ui.toggle()
	_player.set_input_locked(quest_ui.is_open())


func close_furnace_if_open() -> bool:
	var furnace_ui: FurnaceUI = _player.furnace_ui
	if furnace_ui and furnace_ui.visible:
		furnace_ui.close()
		_player.set_input_locked(false)
		return true
	return false


func update_hotbar_display() -> void:
	var hotbar_ui: HotbarUI = _player.hotbar_ui
	if hotbar_ui:
		hotbar_ui.refresh()


func update_connector_prompt() -> void:
	var connector_prompt: CanvasItem = _player.connector_prompt
	var connector_prompt_label: Label = _player.connector_prompt_label
	if connector_prompt == null or connector_prompt_label == null:
		return

	var dimension := _player.get_current_dimension()
	var cell := _player.get_current_cell()
	var text := ""
	var connector_manager: ConnectorManager = _player.connector_manager
	if connector_manager != null:
		var connector: MapConnector = connector_manager.get_connector_at(dimension, cell)
		if connector != null and connector.requires_interaction():
			text = "E  %s" % String(connector.connector_type).replace("_", " ").capitalize()

	if text == "":
		var mechanism_manager: MechanismManager = _player.mechanism_manager
		if mechanism_manager != null:
			var mechanism: MapMechanism = mechanism_manager.get_mechanism_at(dimension, cell)
			if mechanism != null and mechanism.requires_interaction():
				text = "E  %s" % (
						mechanism.action_label
						if mechanism.action_label != ""
						else tr(mechanism.title_key))

	connector_prompt.visible = text != "" and not _player.input_locked
	connector_prompt_label.text = text


func _on_quest_book_closed() -> void:
	_player.set_input_locked(false)


func _on_furnace_ui_closed() -> void:
	_player.set_input_locked(false)


func _on_knapping_ui_closed() -> void:
	_player.set_input_locked(false)


func _on_nei_closed() -> void:
	_player.set_input_locked(false)
