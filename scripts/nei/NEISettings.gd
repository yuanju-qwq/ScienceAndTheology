class_name NEISettingsScript
extends Node

## NEISettings persists NEI (Not Enough Items) configuration across sessions.
## Stores mode, bookmarks, search history, layout and utility toggles.
## Saved to user://nei_settings.cfg so it survives game restarts.

const SAVE_PATH := "user://nei_settings.cfg"
const SECTION := "nei"

# NEI interaction modes — mirrors codechicken.nei layout modes.
enum Mode {
	RECIPE = 0,  # R/U shows recipes/usages, no cheating.
	CHEAT = 1,   # Left click gives a stack, right click gives one.
	UTILITY = 2, # Utility buttons active (time/weather/magnet/etc).
}

# Sidebar anchor position.
enum SidebarSide {
	LEFT = 0,
	RIGHT = 1,
}

const MAX_SEARCH_HISTORY := 20
const MAX_BOOKMARKS := 36

signal settings_changed
signal mode_changed(mode: int)
signal bookmarks_changed
signal utility_toggled(name: String, enabled: bool)

var mode: int = Mode.RECIPE:
	set(value):
		if mode == value:
			return
		mode = value
		mode_changed.emit(mode)
		save()

var sidebar_side: int = SidebarSide.RIGHT:
	set(value):
		if sidebar_side == value:
			return
		sidebar_side = value
		save()

var sidebar_visible: bool = true:
	set(value):
		if sidebar_visible == value:
			return
		sidebar_visible = value
		save()

var show_item_ids: bool = false:
	set(value):
		if show_item_ids == value:
			return
		show_item_ids = value
		save()

var cheat_gives_full_stack: bool = true:
	set(value):
		if cheat_gives_full_stack == value:
			return
		cheat_gives_full_stack = value
		save()

# Bookmarked item ids — quick access row at the top of the sidebar.
var bookmarks: Array[int] = []

# Recent search queries — newest first.
var search_history: Array[String] = []

# Utility feature toggles — active only in UTILITY mode.
var utility_toggles: Dictionary = {
	"magnet": false,
	"infinite": false,
	"delete": false,
	"chunk_loader": false,
	"block_highlight": false,
	"entity_radar": false,
}


func _ready() -> void:
	load_settings()


# Load persisted settings from disk.
func load_settings() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(SAVE_PATH) != OK:
		return
	mode = int(cfg.get_value(SECTION, "mode", Mode.RECIPE))
	sidebar_side = int(cfg.get_value(SECTION, "sidebar_side", SidebarSide.RIGHT))
	sidebar_visible = bool(cfg.get_value(SECTION, "sidebar_visible", true))
	show_item_ids = bool(cfg.get_value(SECTION, "show_item_ids", false))
	cheat_gives_full_stack = bool(cfg.get_value(SECTION, "cheat_gives_full_stack", true))

	bookmarks.clear()
	for raw in cfg.get_value(SECTION, "bookmarks", []) as Array:
		var item_id := int(raw)
		if item_id > 0 and not bookmarks.has(item_id):
			bookmarks.append(item_id)

	search_history.clear()
	for raw in cfg.get_value(SECTION, "search_history", []) as Array:
		var text := str(raw)
		if not text.is_empty():
			search_history.append(text)

	var saved_toggles: Dictionary = cfg.get_value(SECTION, "utility_toggles", {})
	for key in utility_toggles.keys():
		if saved_toggles.has(key):
			utility_toggles[key] = bool(saved_toggles[key])

	settings_changed.emit()


# Persist current settings to disk.
func save() -> void:
	var cfg := ConfigFile.new()
	cfg.set_value(SECTION, "mode", mode)
	cfg.set_value(SECTION, "sidebar_side", sidebar_side)
	cfg.set_value(SECTION, "sidebar_visible", sidebar_visible)
	cfg.set_value(SECTION, "show_item_ids", show_item_ids)
	cfg.set_value(SECTION, "cheat_gives_full_stack", cheat_gives_full_stack)
	cfg.set_value(SECTION, "bookmarks", bookmarks)
	cfg.set_value(SECTION, "search_history", search_history)
	cfg.set_value(SECTION, "utility_toggles", utility_toggles)
	cfg.save(SAVE_PATH)


# Toggle bookmark for an item. Adds if absent, removes if present.
func toggle_bookmark(item_id: int) -> void:
	if item_id <= 0:
		return
	var idx := bookmarks.find(item_id)
	if idx >= 0:
		bookmarks.remove_at(idx)
	else:
		if bookmarks.size() >= MAX_BOOKMARKS:
			bookmarks.remove_at(0)
		bookmarks.append(item_id)
	bookmarks_changed.emit()
	save()


# Returns true if the item is bookmarked.
func is_bookmarked(item_id: int) -> bool:
	return bookmarks.has(item_id)


# Record a search query — moves it to front, deduplicates, trims to max.
func push_search(query: String) -> void:
	query = query.strip_edges()
	if query.is_empty():
		return
	var idx := search_history.find(query)
	if idx >= 0:
		search_history.remove_at(idx)
	search_history.insert(0, query)
	if search_history.size() > MAX_SEARCH_HISTORY:
		search_history.resize(MAX_SEARCH_HISTORY)
	save()


# Clear the search history list.
func clear_search_history() -> void:
	search_history.clear()
	save()


# Toggle a utility feature on/off. Only meaningful in UTILITY mode.
func set_utility(name: String, enabled: bool) -> void:
	if not utility_toggles.has(name):
		return
	if utility_toggles[name] == enabled:
		return
	utility_toggles[name] = enabled
	utility_toggled.emit(name, enabled)
	save()


func get_utility(name: String) -> bool:
	return bool(utility_toggles.get(name, false))


func toggle_utility(name: String) -> void:
	set_utility(name, not get_utility(name))


# Cycle through modes: RECIPE -> CHEAT -> UTILITY -> RECIPE.
func cycle_mode() -> void:
	mode = (mode + 1) % 3
