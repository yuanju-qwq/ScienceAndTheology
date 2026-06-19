# FloatingOrigin — double-precision universe coordinate tracker.
#
# 解决多星球旅行中的浮点精度问题：
# - 宇宙逻辑坐标使用 double（GDScript 的 float 即 64 位 double）。
# - 局部渲染坐标使用 float Vector3（始终保持在原点附近，精度充足）。
# - 当玩家在宇宙中移动较远时，重新居中渲染原点，避免远处物体抖动。
#
# 坐标系约定：
#   universe_position  — 玩家在宇宙中的真实位置（double 精度）。
#   origin             — 当前渲染原点对应的宇宙位置（double 精度）。
#   render_position    — universe_position - origin，作为 float Vector3 用于场景渲染。
#
# 玩家附近永远保持在原点附近（render_position ≈ 0），从而保证体素方块精度。
class_name FloatingOrigin
extends RefCounted

# 玩家的宇宙逻辑坐标（double 精度，GDScript float 即 double）。
var universe_x: float = 0.0
var universe_y: float = 0.0
var universe_z: float = 0.0

# 渲染原点对应的宇宙坐标。场景中 (0,0,0) 对应此宇宙位置。
var origin_x: float = 0.0
var origin_y: float = 0.0
var origin_z: float = 0.0


# 设置玩家的宇宙坐标。
func set_universe_position(x: float, y: float, z: float) -> void:
	universe_x = x
	universe_y = y
	universe_z = z


# 设置玩家的宇宙坐标（从 Vector3，注意 Vector3 是 32 位 float）。
func set_universe_position_vec3(pos: Vector3) -> void:
	universe_x = pos.x
	universe_y = pos.y
	universe_z = pos.z


# 获取玩家的宇宙坐标（返回 Vector3，精度受限于 32 位 float）。
func get_universe_position() -> Vector3:
	return Vector3(universe_x, universe_y, universe_z)


# 获取玩家的宇宙坐标分量（保持 double 精度）。
func get_universe_x() -> float:
	return universe_x

func get_universe_y() -> float:
	return universe_y

func get_universe_z() -> float:
	return universe_z


# 设置渲染原点。
func set_origin(x: float, y: float, z: float) -> void:
	origin_x = x
	origin_y = y
	origin_z = z


func set_origin_vec3(pos: Vector3) -> void:
	origin_x = pos.x
	origin_y = pos.y
	origin_z = pos.z


func get_origin() -> Vector3:
	return Vector3(origin_x, origin_y, origin_z)


# 计算某个宇宙坐标在渲染空间中的位置（float Vector3，接近原点）。
# 用于把远景星球、空间站等放置到正确的相对位置。
func universe_to_render(x: float, y: float, z: float) -> Vector3:
	return Vector3(x - origin_x, y - origin_y, z - origin_z)


func universe_to_render_vec3(pos: Vector3) -> Vector3:
	return Vector3(pos.x - origin_x, pos.y - origin_y, pos.z - origin_z)


# 计算玩家的渲染位置（应始终接近原点）。
func get_render_position() -> Vector3:
	return Vector3(universe_x - origin_x, universe_y - origin_y, universe_z - origin_z)


# 重新居中：将渲染原点设置为当前玩家宇宙坐标。
# 玩家渲染位置归零，所有远景物体需要重新计算相对位置。
func recenter_to_player() -> void:
	origin_x = universe_x
	origin_y = universe_y
	origin_z = universe_z


# 重新居中到指定的宇宙坐标（例如切换到新星球时）。
func recenter_to(x: float, y: float, z: float) -> void:
	origin_x = x
	origin_y = y
	origin_z = z


func recenter_to_vec3(pos: Vector3) -> void:
	origin_x = pos.x
	origin_y = pos.y
	origin_z = pos.z


# 计算两个宇宙坐标之间的距离（double 精度计算，返回 float）。
# 避免大坐标 distance_to 的 float 精度损失。
func universe_distance(ax: float, ay: float, az: float,
		bx: float, by: float, bz: float) -> float:
	var dx: float = ax - bx
	var dy: float = ay - by
	var dz: float = az - bz
	return sqrt(dx * dx + dy * dy + dz * dz)


# 计算玩家到某个宇宙坐标的距离。
func distance_to_player(x: float, y: float, z: float) -> float:
	return universe_distance(universe_x, universe_y, universe_z, x, y, z)


func distance_to_player_vec3(pos: Vector3) -> float:
	return universe_distance(universe_x, universe_y, universe_z, pos.x, pos.y, pos.z)


# 序列化为字典（用于存档）。
func to_dict() -> Dictionary:
	return {
		"universe_x": universe_x,
		"universe_y": universe_y,
		"universe_z": universe_z,
		"origin_x": origin_x,
		"origin_y": origin_y,
		"origin_z": origin_z,
	}


# 从字典反序列化（用于读档）。
func from_dict(d: Dictionary) -> void:
	universe_x = float(d.get("universe_x", 0.0))
	universe_y = float(d.get("universe_y", 0.0))
	universe_z = float(d.get("universe_z", 0.0))
	origin_x = float(d.get("origin_x", 0.0))
	origin_y = float(d.get("origin_y", 0.0))
	origin_z = float(d.get("origin_z", 0.0))
