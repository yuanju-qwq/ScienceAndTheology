class_name ItemDef extends Resource

@export var item_id: int = 0
@export var title_key: String = "ui.unknown"
@export var icon: Texture2D
@export var max_stack: int = 64
@export var tool_stats: ToolDef


func get_display_name() -> String:
	return tr(title_key)


func get_tooltip_lines() -> PackedStringArray:
	var lines := PackedStringArray()
	lines.append(get_display_name())
	if tool_stats != null:
		lines.append("")
		lines.append_array(tool_stats.get_tooltip_lines())
	return lines
