# QuestBookUI — GTNH-style quest book interface.
# Displays chapters as tabs, quests as a grid, and details in a side panel.
# Opens with the J key.
class_name QuestBookUI
extends Control

signal quest_book_opened
signal quest_book_closed

# ============================================================
# Constants
# ============================================================

const PANEL_W := 900
const PANEL_H := 560
const TAB_H := 36
const GRID_W := 280
const DETAIL_X := GRID_W + 16
const QUEST_ICON_SIZE := 40
const QUEST_CELL_SIZE := 52

# State colors per quest state.
const STATE_COLORS: Dictionary = {
	0: Color(0.4, 0.4, 0.4),   # LOCKED — gray
	1: Color(0.9, 0.8, 0.3),   # AVAILABLE — yellow
	2: Color(0.3, 0.7, 1.0),   # IN_PROGRESS — blue
	3: Color(0.3, 1.0, 0.4),   # COMPLETED — green
}

const STATE_LABELS: Dictionary = {
	0: "quest_book.locked",
	1: "quest_book.available",
	2: "quest_book.in_progress",
	3: "quest_book.completed",
}

# ============================================================
# State
# ============================================================

var _is_open := false
var _quest_system: Node = null
var _current_chapter := ""
var _current_quest_id := ""

# ============================================================
# UI nodes
# ============================================================

var _bg: ColorRect
var _title: Label
var _close_btn: Button
var _tab_container: HBoxContainer
var _quest_scroll: ScrollContainer
var _quest_grid: GridContainer
var _detail_panel: VBoxContainer
var _detail_title: Label
var _detail_state: Label
var _detail_desc: Label
var _detail_conditions: VBoxContainer
var _detail_rewards: VBoxContainer
var _detail_claim_btn: Button
var _progress_label: Label

var _tab_buttons: Dictionary = {}   # chapter_id -> Button
var _quest_buttons: Dictionary = {} # quest_id -> Button


# ============================================================
# Lifecycle
# ============================================================

func _ready() -> void:
	visible = false
	_build_ui()
	get_viewport().size_changed.connect(_center_in_viewport)


func _center_in_viewport() -> void:
	position = (get_viewport_rect().size - size) / 2.0


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_ESCAPE and _is_open:
			close()
			get_viewport().set_input_as_handled()


# ============================================================
# Public API
# ============================================================

func set_quest_system(quest_system: Node) -> void:
	_quest_system = quest_system
	if _quest_system:
		_quest_system.quest_unlocked.connect(_on_quest_state_changed)
		_quest_system.quest_completed.connect(_on_quest_state_changed)
		_quest_system.quest_progress_changed.connect(_on_quest_state_changed)
		_quest_system.reward_claimed.connect(_on_quest_state_changed)


func toggle() -> void:
	_is_open = not _is_open
	visible = _is_open
	if _is_open:
		_refresh()
		quest_book_opened.emit()
	else:
		quest_book_closed.emit()


func is_open() -> bool:
	return _is_open


func close() -> void:
	if not _is_open:
		return
	_is_open = false
	visible = false
	quest_book_closed.emit()


# ============================================================
# UI construction
# ============================================================

func _build_ui() -> void:
	size = Vector2(PANEL_W, PANEL_H)
	_center_in_viewport()

	# Background.
	_bg = ColorRect.new()
	_bg.size = size
	_bg.color = Color(0.06, 0.06, 0.08, 0.96)
	add_child(_bg)

	# Title bar.
	_title = Label.new()
	_title.text = tr("quest_book.title")
	_title.position = Vector2(8, 4)
	_title.size = Vector2(200, 22)
	_title.add_theme_color_override("font_color", Color(0.85, 0.9, 1.0))
	_title.add_theme_font_size_override("font_size", 16)
	add_child(_title)

	# Progress label.
	_progress_label = Label.new()
	_progress_label.position = Vector2(220, 4)
	_progress_label.size = Vector2(200, 22)
	_progress_label.add_theme_color_override("font_color", Color(0.6, 0.7, 0.8))
	_progress_label.add_theme_font_size_override("font_size", 14)
	add_child(_progress_label)

	# Close button.
	_close_btn = Button.new()
	_close_btn.text = "X"
	_close_btn.position = Vector2(PANEL_W - 28, 2)
	_close_btn.size = Vector2(24, 24)
	_close_btn.pressed.connect(close)
	add_child(_close_btn)

	# Separator.
	var sep := HSeparator.new()
	sep.position = Vector2(0, 28)
	sep.size = Vector2(PANEL_W, 4)
	add_child(sep)

	# Tab bar.
	_tab_container = HBoxContainer.new()
	_tab_container.position = Vector2(4, 32)
	_tab_container.size = Vector2(PANEL_W - 8, TAB_H)
	_tab_container.add_theme_constant_override("separation", 2)
	add_child(_tab_container)

	# Separator below tabs.
	var sep2 := HSeparator.new()
	sep2.position = Vector2(0, 32 + TAB_H)
	sep2.size = Vector2(PANEL_W, 2)
	add_child(sep2)

	# Quest grid (left side).
	_quest_scroll = ScrollContainer.new()
	_quest_scroll.position = Vector2(4, 32 + TAB_H + 4)
	_quest_scroll.size = Vector2(GRID_W, PANEL_H - _quest_scroll.position.y - 8)
	add_child(_quest_scroll)

	_quest_grid = GridContainer.new()
	_quest_grid.columns = 5
	_quest_grid.add_theme_constant_override("h_separation", 4)
	_quest_grid.add_theme_constant_override("v_separation", 4)
	_quest_scroll.add_child(_quest_grid)

	# Detail panel (right side).
	_build_detail_panel()


func _build_detail_panel() -> void:
	_detail_panel = VBoxContainer.new()
	_detail_panel.position = Vector2(DETAIL_X, 32 + TAB_H + 4)
	_detail_panel.size = Vector2(PANEL_W - DETAIL_X - 8, PANEL_H - _detail_panel.position.y - 8)
	add_child(_detail_panel)

	# Title.
	_detail_title = Label.new()
	_detail_title.add_theme_color_override("font_color", Color(0.9, 0.95, 1.0))
	_detail_title.add_theme_font_size_override("font_size", 18)
	_detail_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(_detail_title)

	# State label.
	_detail_state = Label.new()
	_detail_state.add_theme_color_override("font_color", Color(0.6, 0.7, 0.8))
	_detail_state.add_theme_font_size_override("font_size", 13)
	_detail_state.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(_detail_state)

	# Separator.
	var sep := HSeparator.new()
	sep.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(sep)

	# Description.
	_detail_desc = Label.new()
	_detail_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_detail_desc.add_theme_color_override("font_color", Color(0.8, 0.85, 0.9))
	_detail_desc.add_theme_font_size_override("font_size", 14)
	_detail_desc.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_desc.custom_minimum_size = Vector2(0, 80)
	_detail_panel.add_child(_detail_desc)

	# Conditions section.
	var cond_label := Label.new()
	cond_label.text = tr("quest_book.conditions")
	cond_label.add_theme_color_override("font_color", Color(0.7, 0.8, 1.0))
	cond_label.add_theme_font_size_override("font_size", 14)
	_detail_panel.add_child(cond_label)

	_detail_conditions = VBoxContainer.new()
	_detail_conditions.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(_detail_conditions)

	# Rewards section.
	var rew_sep := HSeparator.new()
	rew_sep.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(rew_sep)

	var rew_label := Label.new()
	rew_label.text = tr("quest_book.rewards")
	rew_label.add_theme_color_override("font_color", Color(1.0, 0.85, 0.4))
	rew_label.add_theme_font_size_override("font_size", 14)
	_detail_panel.add_child(rew_label)

	_detail_rewards = VBoxContainer.new()
	_detail_rewards.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail_panel.add_child(_detail_rewards)

	# Claim reward button.
	_detail_claim_btn = Button.new()
	_detail_claim_btn.text = tr("quest_book.claim_reward")
	_detail_claim_btn.visible = false
	_detail_claim_btn.pressed.connect(_on_claim_reward)
	_detail_panel.add_child(_detail_claim_btn)


# ============================================================
# Refresh
# ============================================================

func _refresh() -> void:
	if not _quest_system:
		return

	# Update progress label.
	var total: int = _quest_system.quest_count()
	var done: int = _quest_system.completed_count()
	_progress_label.text = tr("quest_book.progress") % [done, total]

	# Rebuild tabs.
	_rebuild_tabs()

	# Rebuild quest grid for current chapter.
	if _current_chapter.is_empty():
		var chapters: Array = _quest_system.get_chapters()
		if chapters.size() > 0:
			_current_chapter = chapters[0]
	_rebuild_quest_grid()

	# Show first quest if none selected.
	if _current_quest_id.is_empty():
		var quests: Array = _quest_system.get_quests_in_chapter(_current_chapter)
		if quests.size() > 0:
			_show_quest(quests[0])
	else:
		_show_quest(_current_quest_id)


func _rebuild_tabs() -> void:
	for btn in _tab_buttons.values():
		if is_instance_valid(btn):
			btn.queue_free()
	_tab_buttons.clear()

	if not _quest_system:
		return

	var chapters: Array = _quest_system.get_chapters()
	for chapter_id in chapters:
		var def: Dictionary = _quest_system.get_chapter_def(chapter_id)
		var title: String = def.get("title", chapter_id)
		var btn := Button.new()
		btn.text = tr(title)
		btn.toggle_mode = true
		btn.button_pressed = (chapter_id == _current_chapter)
		btn.pressed.connect(_on_tab_pressed.bind(chapter_id))
		_tab_container.add_child(btn)
		_tab_buttons[chapter_id] = btn


func _rebuild_quest_grid() -> void:
	for btn in _quest_buttons.values():
		if is_instance_valid(btn):
			btn.queue_free()
	_quest_buttons.clear()

	if not _quest_system:
		return

	var quests: Array = _quest_system.get_quests_in_chapter(_current_chapter)
	for quest_id in quests:
		if not _quest_system.is_quest_visible(quest_id):
			continue

		var state: int = _quest_system.get_quest_state(quest_id)
		var def: Dictionary = _quest_system.get_quest_def(quest_id)
		var title: String = def.get("title", quest_id)
		var color: Color = STATE_COLORS.get(state, Color.GRAY)

		var btn := Button.new()
		btn.text = tr(title)
		btn.custom_minimum_size = Vector2(QUEST_CELL_SIZE, QUEST_CELL_SIZE)
		btn.tooltip_text = tr(title)
		btn.add_theme_color_override("font_color", color)
		btn.add_theme_color_override("font_hover_color", color.lightened(0.3))
		btn.toggle_mode = true
		btn.button_pressed = (quest_id == _current_quest_id)
		btn.pressed.connect(_on_quest_pressed.bind(quest_id))
		_quest_grid.add_child(btn)
		_quest_buttons[quest_id] = btn


# ============================================================
# Quest detail display
# ============================================================

func _show_quest(quest_id: String) -> void:
	if not _quest_system:
		return

	var def: Dictionary = _quest_system.get_quest_def(quest_id)
	if def.is_empty():
		return

	_current_quest_id = quest_id

	var state: int = _quest_system.get_quest_state(quest_id)
	var progress: Dictionary = _quest_system.get_quest_progress(quest_id)

	# Title.
	_detail_title.text = tr(def.get("title", quest_id))

	# State.
	var state_label: String = tr(STATE_LABELS.get(state, "quest_book.unknown"))
	_detail_state.text = tr("quest_book.status") % state_label
	_detail_state.add_theme_color_override("font_color", STATE_COLORS.get(state, Color.GRAY))

	# Description.
	_detail_desc.text = tr(def.get("description", ""))

	# Conditions.
	for child in _detail_conditions.get_children():
		child.queue_free()

	var conditions: Array = def.get("conditions", [])
	var progress_data: Dictionary = progress.get("progress", {})
	for cond in conditions:
		var target_key: String = cond.get("target_key", "")
		var target_count: int = cond.get("target_count", 1)
		var cond_key: String = cond.get("condition_key", target_key)
		var current: int = progress_data.get(cond_key, 0)
		var satisfied := current >= target_count

		var label := Label.new()
		label.add_theme_font_size_override("font_size", 13)
		var display_key := tr(target_key)
		if satisfied:
			label.text = tr("quest_book.condition_done") % [display_key, current, target_count]
			label.add_theme_color_override("font_color", Color(0.3, 1.0, 0.4))
		else:
			label.text = tr("quest_book.condition_pending") % [display_key, current, target_count]
			label.add_theme_color_override("font_color", Color(0.8, 0.8, 0.8))
		_detail_conditions.add_child(label)

	# Rewards.
	for child in _detail_rewards.get_children():
		child.queue_free()

	var rewards: Array = def.get("rewards", [])
	for rew in rewards:
		var rew_type: int = rew.get("type", 0)
		var label := Label.new()
		label.add_theme_font_size_override("font_size", 13)
		label.add_theme_color_override("font_color", Color(1.0, 0.85, 0.4))

		match rew_type:
			0:  # ITEM
				var item_key: String = rew.get("item_key", "?")
				var count: int = rew.get("count", 1)
				label.text = tr("quest_book.reward_item") % [item_key, count]
			1:  # SELECT_ONE
				label.text = tr("quest_book.reward_select_one")
			2:  # UNLOCK_QUEST
				var unlock_id: String = rew.get("unlock_quest_id", "?")
				label.text = tr("quest_book.reward_unlock") % unlock_id
			_:
				label.text = tr("quest_book.reward_unknown")

		_detail_rewards.add_child(label)

	# Claim button.
	var reward_claimed: bool = progress.get("reward_claimed", false) if not progress.is_empty() else false
	_detail_claim_btn.visible = (state == 3 and not reward_claimed and rewards.size() > 0)
	_detail_claim_btn.text = tr("quest_book.claim_reward") if not reward_claimed else tr("quest_book.claimed")

	# Highlight in grid.
	for qid in _quest_buttons:
		var btn: Button = _quest_buttons[qid]
		if is_instance_valid(btn):
			btn.button_pressed = (qid == quest_id)


# ============================================================
# Callbacks
# ============================================================

func _on_tab_pressed(chapter_id: String) -> void:
	_current_chapter = chapter_id
	_current_quest_id = ""
	for cid in _tab_buttons:
		var btn: Button = _tab_buttons[cid]
		if is_instance_valid(btn):
			btn.button_pressed = (cid == chapter_id)
	_rebuild_quest_grid()

	# Auto-select first visible quest.
	var quests: Array = _quest_system.get_quests_in_chapter(chapter_id)
	for qid in quests:
		if _quest_system.is_quest_visible(qid):
			_show_quest(qid)
			return


func _on_quest_pressed(quest_id: String) -> void:
	_show_quest(quest_id)


func _on_claim_reward() -> void:
	if not _quest_system or _current_quest_id.is_empty():
		return
	_quest_system.claim_reward(_current_quest_id)
	_show_quest(_current_quest_id)


func _on_quest_state_changed(_quest_id: String) -> void:
	# Refresh the display when quest states change.
	if _is_open:
		_refresh()
