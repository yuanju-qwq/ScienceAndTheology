class_name PlayerAvatarModel
extends Node3D

const SKIN_COLOR := Color(0.88, 0.68, 0.52)
const HAIR_COLOR := Color(0.16, 0.10, 0.06)
const SHIRT_COLOR := Color(0.12, 0.42, 0.78)
const PANTS_COLOR := Color(0.18, 0.24, 0.48)
const BOOT_COLOR := Color(0.10, 0.09, 0.08)
const EYE_COLOR := Color(0.03, 0.03, 0.035)
const MOUTH_COLOR := Color(0.24, 0.08, 0.07)
const ARMOR_COLOR := Color(0.62, 0.64, 0.68)

const LEG_HEIGHT := 0.67
const TORSO_HEIGHT := 0.68
const HEAD_SIZE := 0.45
const TORSO_SIZE := Vector3(0.46, TORSO_HEIGHT, 0.24)
const ARM_SIZE := Vector3(0.16, 0.68, 0.18)
const LEG_SIZE := Vector3(0.20, LEG_HEIGHT, 0.20)
const FOOT_Y := -0.90
const ITEM_BLOCK_SIZE := Vector3(0.18, 0.18, 0.18)
const ITEM_TOOL_SIZE := Vector3(0.055, 0.28, 0.055)
const ITEM_SMALL_SIZE := Vector3(0.10, 0.10, 0.10)

@export var show_head := true:
	set(value):
		show_head = value
		_apply_head_visibility()

@export var show_held_item := true:
	set(value):
		show_held_item = value
		_apply_held_item_visibility()

var _parts: Dictionary = {}
var _right_arm_pivot: Node3D = null
var _left_arm_pivot: Node3D = null
var _held_item: MeshInstance3D = null
var _held_item_id := 0
var _built := false


func _ready() -> void:
	_build_if_needed()


func setup_for_world(local_first_person: bool = true) -> void:
	show_head = not local_first_person
	show_held_item = false
	_build_if_needed()
	_apply_head_visibility()
	_apply_held_item_visibility()


func setup_for_preview() -> void:
	show_head = true
	show_held_item = true
	_build_if_needed()
	rotation = Vector3(0.0, deg_to_rad(18.0), 0.0)
	_apply_head_visibility()
	_apply_held_item_visibility()


func sync_from_player(p) -> void:
	if p == null:
		sync_equipment(null)
		return
	sync_equipment(p.equipment)


func sync_equipment(equipment: GDPlayerEquipment) -> void:
	_build_if_needed()
	if equipment == null:
		set_equipped_item(0)
		_set_overlay_visible("HeadArmor", false)
		_set_overlay_visible("ChestArmor", false)
		_set_overlay_visible("LeftArmArmor", false)
		_set_overlay_visible("RightArmArmor", false)
		_set_overlay_visible("LeftLegArmor", false)
		_set_overlay_visible("RightLegArmor", false)
		_set_overlay_visible("LeftBoot", false)
		_set_overlay_visible("RightBoot", false)
		return

	set_equipped_item(int(equipment.get_equipped(GDPlayerEquipment.SLOT_MAIN_HAND)))
	_update_overlay("HeadArmor", int(equipment.get_equipped(GDPlayerEquipment.SLOT_HEAD)))
	_update_overlay("ChestArmor", int(equipment.get_equipped(GDPlayerEquipment.SLOT_CHEST)))
	var arm_item := int(equipment.get_equipped(GDPlayerEquipment.SLOT_ARM))
	_update_overlay("LeftArmArmor", arm_item)
	_update_overlay("RightArmArmor", arm_item)
	var legs_item := int(equipment.get_equipped(GDPlayerEquipment.SLOT_LEGS))
	_update_overlay("LeftLegArmor", legs_item)
	_update_overlay("RightLegArmor", legs_item)
	var feet_item := int(equipment.get_equipped(GDPlayerEquipment.SLOT_FEET))
	_update_overlay("LeftBoot", feet_item)
	_update_overlay("RightBoot", feet_item)


func set_equipped_item(item_id: int) -> void:
	_build_if_needed()
	_held_item_id = item_id
	if _held_item == null:
		return
	if item_id <= 0:
		_held_item.mesh = null
		_apply_held_item_visibility()
		return

	var shape_size := ITEM_SMALL_SIZE
	match _item_shape(item_id):
		"tool":
			shape_size = ITEM_TOOL_SIZE
		"block":
			shape_size = ITEM_BLOCK_SIZE

	var mesh := BoxMesh.new()
	mesh.size = shape_size
	mesh.material = _make_material(_item_color(item_id))
	_held_item.mesh = mesh
	_held_item.position = Vector3(0.0, -ARM_SIZE.y - 0.07, -0.13)
	_held_item.rotation = Vector3(deg_to_rad(12.0), 0.0, deg_to_rad(-10.0))
	_apply_held_item_visibility()


func _build_if_needed() -> void:
	if _built:
		return
	_built = true

	var leg_y := FOOT_Y + LEG_HEIGHT * 0.5
	var torso_y := FOOT_Y + LEG_HEIGHT + TORSO_HEIGHT * 0.5
	var head_y := FOOT_Y + LEG_HEIGHT + TORSO_HEIGHT + HEAD_SIZE * 0.5
	var arm_shoulder_y := torso_y + TORSO_HEIGHT * 0.5

	_add_box("Torso", TORSO_SIZE, Vector3(0.0, torso_y, 0.0), SHIRT_COLOR, self)
	_add_box("Head", Vector3(HEAD_SIZE, HEAD_SIZE, HEAD_SIZE), Vector3(0.0, head_y, 0.0), SKIN_COLOR, self)
	_add_box("Hair", Vector3(HEAD_SIZE + 0.025, 0.16, HEAD_SIZE + 0.025),
			Vector3(0.0, head_y + HEAD_SIZE * 0.33, 0.0), HAIR_COLOR, self)

	_add_box("LeftEye", Vector3(0.055, 0.035, 0.012),
			Vector3(-0.085, head_y + 0.055, -HEAD_SIZE * 0.5 - 0.006), EYE_COLOR, self)
	_add_box("RightEye", Vector3(0.055, 0.035, 0.012),
			Vector3(0.085, head_y + 0.055, -HEAD_SIZE * 0.5 - 0.006), EYE_COLOR, self)
	_add_box("Mouth", Vector3(0.12, 0.025, 0.012),
			Vector3(0.0, head_y - 0.075, -HEAD_SIZE * 0.5 - 0.006), MOUTH_COLOR, self)

	_left_arm_pivot = Node3D.new()
	_left_arm_pivot.name = "LeftArmPivot"
	_left_arm_pivot.position = Vector3(-0.32, arm_shoulder_y - 0.03, 0.0)
	_left_arm_pivot.rotation.z = deg_to_rad(-3.0)
	add_child(_left_arm_pivot)
	_parts["LeftArmPivot"] = _left_arm_pivot
	_add_box("LeftArm", ARM_SIZE, Vector3(0.0, -ARM_SIZE.y * 0.5, 0.0), SKIN_COLOR, _left_arm_pivot)

	_right_arm_pivot = Node3D.new()
	_right_arm_pivot.name = "RightArmPivot"
	_right_arm_pivot.position = Vector3(0.32, arm_shoulder_y - 0.03, 0.0)
	_right_arm_pivot.rotation.z = deg_to_rad(3.0)
	add_child(_right_arm_pivot)
	_parts["RightArmPivot"] = _right_arm_pivot
	_add_box("RightArm", ARM_SIZE, Vector3(0.0, -ARM_SIZE.y * 0.5, 0.0), SKIN_COLOR, _right_arm_pivot)

	_held_item = MeshInstance3D.new()
	_held_item.name = "HeldItem"
	_right_arm_pivot.add_child(_held_item)
	_parts["HeldItem"] = _held_item

	_add_box("LeftLeg", LEG_SIZE, Vector3(-0.11, leg_y, 0.0), PANTS_COLOR, self)
	_add_box("RightLeg", LEG_SIZE, Vector3(0.11, leg_y, 0.0), PANTS_COLOR, self)
	_add_box("LeftFoot", Vector3(0.205, 0.12, 0.215),
			Vector3(-0.11, FOOT_Y + 0.06, -0.01), BOOT_COLOR, self)
	_add_box("RightFoot", Vector3(0.205, 0.12, 0.215),
			Vector3(0.11, FOOT_Y + 0.06, -0.01), BOOT_COLOR, self)

	_add_box("HeadArmor", Vector3(HEAD_SIZE + 0.045, HEAD_SIZE + 0.045, HEAD_SIZE + 0.045),
			Vector3(0.0, head_y, 0.0), ARMOR_COLOR, self)
	_add_box("ChestArmor", TORSO_SIZE + Vector3(0.045, 0.045, 0.045),
			Vector3(0.0, torso_y, 0.0), ARMOR_COLOR, self)
	_add_box("LeftArmArmor", ARM_SIZE + Vector3(0.035, 0.035, 0.035),
			Vector3(0.0, -ARM_SIZE.y * 0.5, 0.0), ARMOR_COLOR, _left_arm_pivot)
	_add_box("RightArmArmor", ARM_SIZE + Vector3(0.035, 0.035, 0.035),
			Vector3(0.0, -ARM_SIZE.y * 0.5, 0.0), ARMOR_COLOR, _right_arm_pivot)
	_add_box("LeftLegArmor", LEG_SIZE + Vector3(0.035, 0.035, 0.035),
			Vector3(-0.11, leg_y, 0.0), ARMOR_COLOR, self)
	_add_box("RightLegArmor", LEG_SIZE + Vector3(0.035, 0.035, 0.035),
			Vector3(0.11, leg_y, 0.0), ARMOR_COLOR, self)
	_add_box("LeftBoot", Vector3(0.23, 0.16, 0.24),
			Vector3(-0.11, FOOT_Y + 0.08, -0.01), ARMOR_COLOR, self)
	_add_box("RightBoot", Vector3(0.23, 0.16, 0.24),
			Vector3(0.11, FOOT_Y + 0.08, -0.01), ARMOR_COLOR, self)

	for overlay_name in [
		"HeadArmor",
		"ChestArmor",
		"LeftArmArmor",
		"RightArmArmor",
		"LeftLegArmor",
		"RightLegArmor",
		"LeftBoot",
		"RightBoot",
	]:
		_set_overlay_visible(overlay_name, false)

	_apply_head_visibility()
	_apply_held_item_visibility()


func _add_box(
		part_name: String,
		box_size: Vector3,
		box_position: Vector3,
		color: Color,
		parent: Node) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = box_size
	mesh.material = _make_material(color)
	var part := MeshInstance3D.new()
	part.name = part_name
	part.mesh = mesh
	part.position = box_position
	part.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	parent.add_child(part)
	_parts[part_name] = part
	return part


func _make_material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = 0.92
	material.disable_fog = true
	return material


func _apply_head_visibility() -> void:
	for part_name in ["Head", "Hair", "LeftEye", "RightEye", "Mouth", "HeadArmor"]:
		var part := _parts.get(part_name) as Node3D
		if part != null:
			part.visible = show_head and (part_name != "HeadArmor" or part.visible)


func _apply_held_item_visibility() -> void:
	if _held_item != null:
		_held_item.visible = show_held_item and _held_item_id > 0


func _set_overlay_visible(part_name: String, is_visible: bool) -> void:
	var part := _parts.get(part_name) as MeshInstance3D
	if part != null:
		part.visible = is_visible and (part_name != "HeadArmor" or show_head)


func _update_overlay(part_name: String, item_id: int) -> void:
	var part := _parts.get(part_name) as MeshInstance3D
	if part == null:
		return
	part.visible = item_id > 0 and (part_name != "HeadArmor" or show_head)
	if item_id > 0:
		part.material_override = _make_material(_item_color(item_id).lerp(ARMOR_COLOR, 0.55))


func _item_color(item_id: int) -> Color:
	var tool := ItemDatabase.get_tool_stats(item_id)
	if tool:
		var material_key := tool.material_key.to_lower()
		if material_key.contains("wood"):
			return Color(0.55, 0.35, 0.15)
		if material_key.contains("stone"):
			return Color(0.60, 0.60, 0.60)
		if material_key.contains("iron"):
			return Color(0.75, 0.75, 0.80)
		if material_key.contains("copper"):
			return Color(0.80, 0.45, 0.20)
		if material_key.contains("bronze"):
			return Color(0.70, 0.55, 0.20)
		if material_key.contains("steel"):
			return Color(0.55, 0.58, 0.62)
		return Color(0.62, 0.62, 0.62)

	match item_id:
		ItemDatabase.ITEM_WORKBENCH:
			return Color(0.50, 0.35, 0.20)
		ItemDatabase.ITEM_FURNACE:
			return Color(0.45, 0.35, 0.25)
		ItemDatabase.ITEM_LADDER:
			return Color(0.55, 0.30, 0.15)
		ItemDatabase.ITEM_FENCE:
			return Color(0.50, 0.32, 0.16)
		ItemDatabase.ITEM_COAL_BLOCK:
			return Color(0.10, 0.10, 0.11)
		ItemDatabase.ITEM_FIREBRICK:
			return Color(0.58, 0.24, 0.12)
		ItemDatabase.ITEM_ANVIL:
			return Color(0.30, 0.30, 0.32)
		ItemDatabase.ITEM_STONE_PLATE:
			return Color(0.46, 0.46, 0.47)
		ItemDatabase.ITEM_WOOD_PLATE:
			return Color(0.58, 0.38, 0.18)
		ItemDatabase.ITEM_CHARCOAL:
			return Color(0.12, 0.12, 0.12)
		ItemDatabase.ITEM_IRON_BLOOM:
			return Color(0.55, 0.35, 0.15)
		ItemDatabase.ITEM_WROUGHT_IRON_INGOT:
			return Color(0.52, 0.50, 0.48)
		ItemDatabase.ITEM_STEEL_INGOT:
			return Color(0.55, 0.58, 0.62)
		ItemDatabase.ITEM_COPPER_PICKAXE, ItemDatabase.ITEM_COPPER_AXE, ItemDatabase.ITEM_COPPER_SHOVEL, ItemDatabase.ITEM_COPPER_SWORD:
			return Color(0.80, 0.45, 0.20)
		ItemDatabase.ITEM_TIN_BRONZE_PICKAXE, ItemDatabase.ITEM_TIN_BRONZE_AXE, ItemDatabase.ITEM_BISMUTH_BRONZE_PICKAXE, ItemDatabase.ITEM_BLACK_BRONZE_PICKAXE:
			return Color(0.70, 0.55, 0.20)
		ItemDatabase.ITEM_STEEL_PICKAXE, ItemDatabase.ITEM_STEEL_AXE, ItemDatabase.ITEM_STEEL_SHOVEL, ItemDatabase.ITEM_STEEL_SWORD:
			return Color(0.55, 0.58, 0.62)
	return Color(0.60, 0.40, 0.20)


func _item_shape(item_id: int) -> String:
	if ItemDatabase.get_tool_stats(item_id):
		return "tool"
	match item_id:
		ItemDatabase.ITEM_WORKBENCH, ItemDatabase.ITEM_FURNACE, ItemDatabase.ITEM_LADDER, ItemDatabase.ITEM_FENCE, ItemDatabase.ITEM_COAL_BLOCK, ItemDatabase.ITEM_FIREBRICK, ItemDatabase.ITEM_ANVIL, ItemDatabase.ITEM_STONE_PLATE, ItemDatabase.ITEM_WOOD_PLATE:
			return "block"
	return "small"
