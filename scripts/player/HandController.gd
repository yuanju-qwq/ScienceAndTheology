class_name HandController
extends Node3D

const ARM_COLOR := Color(0.92, 0.82, 0.72)
const HAND_POS := Vector3(0.2, -0.2, -0.4)
const FOREARM_LEN := 0.18
const FOREARM_WIDTH := 0.055
const HAND_SIZE := Vector3(0.07, 0.035, 0.055)
const SWING_DURATION := 0.2
const SWING_ANGLE := deg_to_rad(45.0)
const ITEM_BLOCK := Vector3(0.12, 0.12, 0.12)
const ITEM_TOOL := Vector3(0.04, 0.14, 0.04)
const ITEM_SMALL := Vector3(0.06, 0.06, 0.06)

var _shoulder: Node3D
var _forearm: MeshInstance3D
var _hand: MeshInstance3D
var _held_item: MeshInstance3D
var _is_swinging := false
var _swing_progress := 0.0
var _rest_rot: Vector3

func _init():
	name = "HandController"
	_shoulder = Node3D.new()
	_shoulder.name = "Shoulder"
	add_child(_shoulder)
	_rest_rot = Vector3(deg_to_rad(-15), deg_to_rad(5), deg_to_rad(-3))
	_shoulder.position = HAND_POS
	_shoulder.rotation = _rest_rot

	_forearm = MeshInstance3D.new()
	_forearm.name = "Forearm"
	var fm := BoxMesh.new()
	fm.size = Vector3(FOREARM_WIDTH, FOREARM_LEN, FOREARM_WIDTH)
	var fmat := StandardMaterial3D.new()
	fmat.albedo_color = ARM_COLOR
	fmat.disable_fog = true
	fmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	fm.material = fmat
	_forearm.mesh = fm
	_forearm.position = Vector3(0, -FOREARM_LEN / 2.0, 0)
	_shoulder.add_child(_forearm)

	_hand = MeshInstance3D.new()
	_hand.name = "Hand"
	var hm := BoxMesh.new()
	hm.size = HAND_SIZE
	var hmat := StandardMaterial3D.new()
	hmat.albedo_color = ARM_COLOR
	hmat.disable_fog = true
	hmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	hm.material = hmat
	_hand.mesh = hm
	_hand.position = Vector3(0.005, -FOREARM_LEN, 0)
	_shoulder.add_child(_hand)

	_held_item = MeshInstance3D.new()
	_held_item.name = "HeldItem"
	_shoulder.add_child(_held_item)
	visible = false

func set_item(item_id: int) -> void:
	if item_id <= 0:
		visible = false
		return
	visible = true

	var color := _item_color(item_id)
	var shape_name := _item_shape(item_id)
	var offset_y := -0.04
	var shape_size := ITEM_SMALL
	match shape_name:
		"tool":
			shape_size = ITEM_TOOL
			offset_y = -0.1
		"block":
			shape_size = ITEM_BLOCK
			offset_y = -0.06

	var mesh := BoxMesh.new()
	mesh.size = shape_size
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.disable_fog = true
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mesh.material = mat
	_held_item.mesh = mesh
	_held_item.position = Vector3(0, -FOREARM_LEN + offset_y, -0.05)

func swing() -> void:
	if _is_swinging:
		return
	_is_swinging = true
	_swing_progress = 0.0

func _process(delta: float) -> void:
	if _is_swinging:
		_swing_progress += delta / SWING_DURATION
		if _swing_progress >= 1.0:
			_is_swinging = false
			_shoulder.rotation = _rest_rot
		else:
			var t := _swing_progress
			var angle := -sin(t * PI) * SWING_ANGLE
			_shoulder.rotation = _rest_rot + Vector3(angle, 0, 0)

func _item_color(item_id: int) -> Color:
	var tool := ItemDatabase.get_tool_stats(item_id)
	if tool:
		var mk := tool.material_key.to_lower()
		if mk.contains("wood"):
			return Color(0.55, 0.35, 0.15)
		if mk.contains("stone"):
			return Color(0.6, 0.6, 0.6)
		if mk.contains("iron"):
			return Color(0.75, 0.75, 0.8)
		if mk.contains("copper"):
			return Color(0.8, 0.45, 0.2)
		if mk.contains("bronze"):
			return Color(0.7, 0.55, 0.2)
		if mk.contains("steel"):
			return Color(0.55, 0.58, 0.62)
		return Color(0.6, 0.6, 0.6)
	match item_id:
		ItemDatabase.ITEM_WORKBENCH:
			return Color(0.5, 0.35, 0.2)
		ItemDatabase.ITEM_FURNACE:
			return Color(0.45, 0.35, 0.25)
		ItemDatabase.ITEM_LADDER:
			return Color(0.55, 0.3, 0.15)
		ItemDatabase.ITEM_FENCE:
			return Color(0.5, 0.32, 0.16)
		ItemDatabase.ITEM_COAL_BLOCK:
			return Color(0.1, 0.1, 0.11)
		ItemDatabase.ITEM_FIREBRICK:
			return Color(0.58, 0.24, 0.12)
		ItemDatabase.ITEM_ANVIL:
			return Color(0.3, 0.3, 0.32)
		ItemDatabase.ITEM_STONE_PLATE:
			return Color(0.46, 0.46, 0.47)
		ItemDatabase.ITEM_WOOD_PLATE:
			return Color(0.58, 0.38, 0.18)
		ItemDatabase.ITEM_CHARCOAL:
			return Color(0.12, 0.12, 0.12)
		ItemDatabase.ITEM_IRON_BLOOM:
			return Color(0.55, 0.35, 0.15)
		ItemDatabase.ITEM_WROUGHT_IRON_INGOT:
			return Color(0.52, 0.5, 0.48)
		ItemDatabase.ITEM_STEEL_INGOT:
			return Color(0.55, 0.58, 0.62)
		ItemDatabase.ITEM_COPPER_PICKAXE, ItemDatabase.ITEM_COPPER_AXE, ItemDatabase.ITEM_COPPER_SHOVEL, ItemDatabase.ITEM_COPPER_SWORD:
			return Color(0.8, 0.45, 0.2)
		ItemDatabase.ITEM_TIN_BRONZE_PICKAXE, ItemDatabase.ITEM_TIN_BRONZE_AXE, ItemDatabase.ITEM_BISMUTH_BRONZE_PICKAXE, ItemDatabase.ITEM_BLACK_BRONZE_PICKAXE:
			return Color(0.7, 0.55, 0.2)
		ItemDatabase.ITEM_STEEL_PICKAXE, ItemDatabase.ITEM_STEEL_AXE, ItemDatabase.ITEM_STEEL_SHOVEL, ItemDatabase.ITEM_STEEL_SWORD:
			return Color(0.55, 0.58, 0.62)
	return Color(0.6, 0.4, 0.2)

func _item_shape(item_id: int) -> String:
	if ItemDatabase.get_tool_stats(item_id):
		return "tool"
	match item_id:
		ItemDatabase.ITEM_WORKBENCH, ItemDatabase.ITEM_FURNACE, ItemDatabase.ITEM_LADDER, ItemDatabase.ITEM_FENCE, ItemDatabase.ITEM_COAL_BLOCK, ItemDatabase.ITEM_FIREBRICK, ItemDatabase.ITEM_ANVIL, ItemDatabase.ITEM_STONE_PLATE, ItemDatabase.ITEM_WOOD_PLATE:
			return "block"
	return "small"
