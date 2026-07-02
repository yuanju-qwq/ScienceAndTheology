# ProbePanel — A The-One-Probe-style HUD overlay that displays detailed
# information about the block the player is currently looking at.
# Default mode shows compact info (name, hardness, tool, mining level).
# Press F3 to toggle full detail mode (key, drops, flags, tool match).
class_name ProbePanel
extends Control

# Compact mode: name, hardness, tool requirement, mining level.
const MODE_COMPACT := 0
# Full mode: all fields including key, drops, flags, tool match status.
const MODE_FULL := 1

# Background style constants.
const BG_COLOR := Color(0.035, 0.04, 0.045, 0.78)
const BG_PADDING_H := 10.0
const BG_PADDING_V := 6.0
const LINE_SPACING := 4.0
const FONT_SIZE := 14

# Current display mode.
var display_mode := MODE_COMPACT:
	set(value):
		if display_mode == value:
			return
		display_mode = value
		_needs_rebuild = true

# Whether the panel has valid target data to show.
var has_target := false:
	set(value):
		if has_target == value:
			return
		has_target = value
		_needs_rebuild = true

# Cached material definition dictionary from GDWorldData.
var _mat_def: Dictionary = {}
# Current equipped tool stats for match checking.
var _tool_def: ToolDef = null

var _needs_rebuild := true

# UI nodes.
var _bg: ColorRect
var _name_label: Label
var _info_label: Label
var _detail_label: Label


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	anchor_left = 0.5
	anchor_right = 0.5
	anchor_top = 0.0
	anchor_bottom = 0.0
	offset_left = -160.0
	offset_right = 160.0
	offset_top = 8.0
	offset_bottom = 80.0

	_build_ui()


func _process(_delta: float) -> void:
	if _needs_rebuild:
		_rebuild()
		_needs_rebuild = false

	visible = has_target and not _mat_def.is_empty()


# Build the static UI structure.
func _build_ui() -> void:
	_bg = ColorRect.new()
	_bg.color = BG_COLOR
	_bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_bg)

	_name_label = Label.new()
	_name_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_name_label.add_theme_font_size_override("font_size", FONT_SIZE + 2)
	_name_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_name_label)

	_info_label = Label.new()
	_info_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_info_label.add_theme_font_size_override("font_size", FONT_SIZE)
	_info_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_info_label)

	_detail_label = Label.new()
	_detail_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_detail_label.add_theme_font_size_override("font_size", FONT_SIZE - 2)
	_detail_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_detail_label)


# Update the panel with a new material definition and tool info.
func update_target(mat_def: Dictionary, tool_def: ToolDef) -> void:
	var changed := _mat_def != mat_def or _tool_def != tool_def
	_mat_def = mat_def
	_tool_def = tool_def
	has_target = not mat_def.is_empty()
	if changed:
		_needs_rebuild = true


# Clear the target display.
func clear_target() -> void:
	if has_target:
		has_target = false
		_mat_def = {}
		_tool_def = null
		_needs_rebuild = true


# Toggle between compact and full display modes.
func toggle_mode() -> void:
	display_mode = MODE_FULL if display_mode == MODE_COMPACT else MODE_COMPACT


# Rebuild all label text and reposition elements.
func _rebuild() -> void:
	if _mat_def.is_empty():
		_bg.visible = false
		_name_label.text = ""
		_info_label.text = ""
		_detail_label.text = ""
		return

	_bg.visible = true

	var name_key: String = _mat_def.get("title_key", "ui.unknown")
	var hardness: float = _mat_def.get("hardness", 0.0)
	var tool_tag: String = _mat_def.get("required_tool_tag", "")
	var mining_level: int = _mat_def.get("required_mining_level", 0)

	# Name line.
	_name_label.text = tr(name_key)

	# Compact info line: hardness + tool + level.
	var info_parts: PackedStringArray = []
	if hardness >= 0.0:
		info_parts.append(tr("probe.hardness") % hardness)
	else:
		info_parts.append(tr("probe.indestructible"))
	if tool_tag != "":
		info_parts.append(tr("probe.tool") % tool_tag.capitalize())
	if mining_level > 0:
		info_parts.append(tr("probe.level") % mining_level)
	_info_label.text = "  |  ".join(info_parts)

	# Detail line (only in full mode).
	if display_mode == MODE_FULL:
		var detail_parts: PackedStringArray = []

		# Key.
		var key: String = _mat_def.get("key", "")
		if key != "":
			detail_parts.append(tr("probe.key") % key)

		# Drops.
		var drops: Array = _mat_def.get("drops", [])
		if drops.size() > 0:
			var drop_names: PackedStringArray = []
			for drop in drops:
				var item_key: String = drop.get("item_key", "?")
				var count: int = drop.get("count", 1)
				if count > 1:
					drop_names.append("%s x%d" % [item_key, count])
				else:
					drop_names.append(item_key)
			detail_parts.append(tr("probe.drops") % ", ".join(drop_names))
		else:
			detail_parts.append(tr("probe.drops_none"))

		# Flags.
		var flags: int = int(_mat_def.get("flags", 0))
		var flag_names: PackedStringArray = []
		if flags & 0x01:
			flag_names.append(tr("probe.flag_walkable"))
		if flags & 0x02:
			flag_names.append(tr("probe.flag_solid"))
		if flags & 0x04:
			flag_names.append(tr("probe.flag_liquid"))
		if flags & 0x08:
			flag_names.append(tr("probe.flag_mineable"))
		if flags & 0x10:
			flag_names.append(tr("probe.flag_climbable"))
		if flags & 0x20:
			flag_names.append(tr("probe.indestructible"))
		if flags & 0x40:
			flag_names.append(tr("probe.flag_gravity"))
		if flags & 0x80:
			flag_names.append(tr("probe.flag_collapse"))
		if flags & 0x100:
			flag_names.append(tr("probe.flag_support"))
		if flag_names.size() > 0:
			detail_parts.append(tr("probe.flags") % ", ".join(flag_names))

		# Tool match status.
		if tool_tag != "" and _tool_def != null:
			var matches := ToolDef.check_tool_match(_tool_def, tool_tag, mining_level)
			if matches:
				detail_parts.append(tr("probe.tool_match"))
			else:
				detail_parts.append(tr("probe.tool_mismatch"))
		elif tool_tag != "" and _tool_def == null:
			detail_parts.append(tr("probe.tool_none"))

		_detail_label.text = "  |  ".join(detail_parts)
		_detail_label.visible = true
	else:
		_detail_label.text = ""
		_detail_label.visible = false

	_layout()


# Position all child nodes based on content.
func _layout() -> void:
	var y := BG_PADDING_V
	var max_width := 0.0

	# Name label.
	_name_label.position = Vector2(BG_PADDING_H, y)
	_name_label.size = Vector2(size.x - BG_PADDING_H * 2, 0)
	_name_label.reset_size()
	y += _name_label.size.y + LINE_SPACING
	max_width = maxf(max_width, _name_label.size.x + BG_PADDING_H * 2)

	# Info label.
	_info_label.position = Vector2(BG_PADDING_H, y)
	_info_label.size = Vector2(size.x - BG_PADDING_H * 2, 0)
	_info_label.reset_size()
	y += _info_label.size.y + LINE_SPACING
	max_width = maxf(max_width, _info_label.size.x + BG_PADDING_H * 2)

	# Detail label.
	if _detail_label.visible and _detail_label.text != "":
		_detail_label.position = Vector2(BG_PADDING_H, y)
		_detail_label.size = Vector2(size.x - BG_PADDING_H * 2, 0)
		_detail_label.reset_size()
		y += _detail_label.size.y + LINE_SPACING
		max_width = maxf(max_width, _detail_label.size.x + BG_PADDING_H * 2)

	y += BG_PADDING_V

	# Background.
	_bg.position = Vector2.ZERO
	_bg.size = Vector2(max_width, y)

	# Center the panel horizontally.
	offset_left = -max_width / 2.0
	offset_right = max_width / 2.0
	offset_top = 8.0
	offset_bottom = 8.0 + y
