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
	var mat_display := tr(material_key) if not material_key.is_empty() else tr("tooltip.unknown_material")
	lines.append(mat_display + " " + tr(type_name))
	lines.append(tr("tooltip.type_mining_level") % [tr(type_name), mining_level])
	lines.append(tr("tooltip.speed_durability") % [speed, durability])
	lines.append(tr("tooltip.attack") % [attack_damage])
	return lines
