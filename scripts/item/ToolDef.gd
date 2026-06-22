class_name ToolDef extends Resource

enum ToolType { NONE = 0, PICKAXE, AXE, SHOVEL, SWORD, HOE, KNIFE, SPEAR }

@export var tool_type: int = ToolType.NONE
@export var mining_level: int = 0
@export var material_key: String = ""
@export var speed: float = 1.0
@export var durability: int = 0
@export var attack_damage: float = 1.0


# Check whether the given tool matches the block's required tool tag and mining level.
# Returns true if no tool is required, or if the tool's type and mining level
# satisfy the requirement.
static func check_tool_match(
		tool: ToolDef, required_tag: String,
		required_level: int) -> bool:
	if required_tag == "":
		return true
	if tool == null:
		return false
	var tool_type_name: String = str(ToolType.keys()[tool.tool_type]).to_lower()
	if tool_type_name != required_tag.to_lower():
		return false
	if tool.mining_level < required_level:
		return false
	return true


# Return tooltip lines for display.
func get_tooltip_lines() -> PackedStringArray:
	var lines := PackedStringArray()
	var type_name := str(ToolType.keys()[tool_type])
	var mat_display := material_key if not material_key.is_empty() else "Unknown"
	lines.append("%s %s" % [mat_display, type_name])
	lines.append("Type: %s  |  Mining Level: %d" % [type_name, mining_level])
	lines.append("Speed: %.1f  |  Durability: %d" % [speed, durability])
	lines.append("Attack: %.1f" % [attack_damage])
	return lines
