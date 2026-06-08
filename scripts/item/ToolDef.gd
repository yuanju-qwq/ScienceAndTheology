class_name ToolDef extends Resource

enum ToolType { NONE = 0, PICKAXE, AXE, SHOVEL, SWORD }
enum Tier { WOOD = 0, STONE, IRON, DIAMOND }

@export var tool_type: int = ToolType.NONE
@export var tier: int = Tier.WOOD
@export var mining_level: int = 0
@export var speed: float = 1.0
@export var durability: int = 0
@export var attack_damage: float = 1.0
