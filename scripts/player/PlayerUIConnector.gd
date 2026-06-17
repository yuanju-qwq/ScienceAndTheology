# PlayerUIConnector — Manages all UI toggle and connection logic for the player.
# Extracted from PlayerController to separate UI wiring from gameplay.
class_name PlayerUIConnector
extends Node

var _player: PlayerController = null


func setup(player: PlayerController) -> void:
	_player = player


func connect_ui() -> void:
	var hotbar_ui = _player.hotbar_ui
	var inventory_ui = _player.inventory_ui
	var crafting_ui = _player.crafting_ui
	var furnace_ui = _player.furnace_ui

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


func toggle_inventory() -> void:
	var crafting_ui = _player.crafting_ui
	var inventory_ui = _player.inventory_ui
	if crafting_ui and crafting_ui.visible:
		toggle_crafting()
	if inventory_ui:
		inventory_ui.toggle()


func toggle_wiki() -> void:
	var crafting_ui = _player.crafting_ui
	var inventory_ui = _player.inventory_ui
	var wiki_ui = _player.wiki_ui
	if crafting_ui and crafting_ui.visible:
		toggle_crafting()
	if inventory_ui and inventory_ui.visible:
		inventory_ui.toggle()
	if wiki_ui:
		wiki_ui.toggle()
		_player._set_input_locked(wiki_ui.visible)


func toggle_crafting(station: String = "") -> void:
	var crafting_ui = _player.crafting_ui
	var inventory_ui = _player.inventory_ui
	if crafting_ui == null:
		return
	if inventory_ui and inventory_ui.visible:
		inventory_ui.toggle()
	if station == "":
		station = _player._interaction.get_nearby_station()
	crafting_ui.set_station(station)
	crafting_ui.toggle()
	_player._set_input_locked(crafting_ui.visible)


func close_furnace_if_open() -> bool:
	var furnace_ui = _player.furnace_ui
	if furnace_ui and furnace_ui.visible:
		furnace_ui.close()
		_player._set_input_locked(false)
		return true
	return false


func update_hotbar_display() -> void:
	var hotbar_ui = _player.hotbar_ui
	if hotbar_ui:
		hotbar_ui.refresh()


func update_connector_prompt() -> void:
	var connector_prompt = _player.connector_prompt
	var connector_prompt_label = _player.connector_prompt_label
	if connector_prompt == null or connector_prompt_label == null:
		return

	var dimension := _player.get_current_dimension()
	var cell := _player.get_current_cell()
	var text := ""
	var connector_manager: ConnectorManager = _player.connector_manager
	if connector_manager != null:
		var connector = connector_manager.get_connector_at(dimension, cell)
		if connector != null and connector.requires_interaction():
			text = "E  %s" % String(connector.connector_type).replace("_", " ").capitalize()

	if text == "":
		var mechanism_manager: MechanismManager = _player.mechanism_manager
		if mechanism_manager != null:
			var mechanism = mechanism_manager.get_mechanism_at(dimension, cell)
			if mechanism != null and mechanism.requires_interaction():
				text = "E  %s" % (mechanism.action_label if mechanism.action_label != "" else mechanism.display_name)

	connector_prompt.visible = text != "" and not _player._input_locked
	connector_prompt_label.text = text


func _on_furnace_ui_closed() -> void:
	_player._set_input_locked(false)
