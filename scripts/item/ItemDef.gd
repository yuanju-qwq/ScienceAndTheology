class_name ItemDef extends Resource

enum Category {
	MATERIALS = 0,
	TOOLS = 1,
	COMPONENTS = 2,
	PLACEABLES = 3,
	RESOURCES = 4,
	FOOD = 5,
	MISC = 6,
}

@export var item_id: int = 0
@export var title_key: String = "ui.unknown"
@export var icon: Texture2D
@export var max_stack: int = 64
@export var tool_stats: ToolDef
@export var category: Category = Category.MISC


func get_display_name() -> String:
	return tr(title_key)


func get_tooltip_lines() -> PackedStringArray:
	var lines := PackedStringArray()
	lines.append(get_display_name())
	if tool_stats != null:
		lines.append("")
		lines.append_array(tool_stats.get_tooltip_lines())
	return lines
