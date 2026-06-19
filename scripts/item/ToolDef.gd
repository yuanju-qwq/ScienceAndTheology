class_name ToolDef extends Resource

enum ToolType { NONE = 0, PICKAXE, AXE, SHOVEL, SWORD }
enum Tier { WOOD = 0, STONE, IRON, DIAMOND }

@export var tool_type: int = ToolType.NONE
@export var tier: int = Tier.WOOD
@export var mining_level: int = 0
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
