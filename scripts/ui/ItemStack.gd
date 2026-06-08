class_name ItemStack extends Resource

@export var item_id: int = 0
@export var count: int = 0

func _init(p_item_id: int = 0, p_count: int = 0):
	item_id = p_item_id
	count = p_count

func is_empty() -> bool:
	return item_id == 0 or count <= 0

func get_item_def() -> ItemDef:
	if item_id == 0:
		return null
	return ItemDatabase.get_item(item_id)

func get_display_name() -> String:
	var def := get_item_def()
	return def.display_name if def else "Unknown"

func get_icon() -> Texture2D:
	var def := get_item_def()
	return def.icon if def else null

func get_max_stack() -> int:
	var def := get_item_def()
	return def.max_stack if def else 64

func can_stack_with(other: ItemStack) -> bool:
	return item_id == other.item_id
