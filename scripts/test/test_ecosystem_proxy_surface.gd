extends SceneTree

const SIZE := 32
const SURFACE_Y := 24
const TF_SOLID := 1 << 1

var _failed := false


func _init() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	var world := GDWorldData.new()
	world.set_chunk_from_dict("test", 0, 0, 0, _make_chunk(true))
	world.set_chunk_from_dict("test", 0, 1, 0, _make_chunk(false))

	var tick_system := GDTickSystem.new()
	tick_system.set_world_data(world)
	tick_system.register_ecosystem_system()
	tick_system.set_active_radius(1)
	tick_system.add_player_chunk(1, "test", 0, 0, 0)
	tick_system.tick(0.05)

	var air_proxies: Array = tick_system.get_proxy_data("test", 0, 1, 0)
	_expect(air_proxies.is_empty(), "air chunk spawned wild proxies")

	var ground_proxies: Array = tick_system.get_proxy_data("test", 0, 0, 0)
	_expect(ground_proxies.size() > 0, "surface chunk spawned no proxies")
	_expect(ground_proxies.size() <= 2, "surface chunk spawned too many proxies")

	for proxy in ground_proxies:
		var y := int(round(float(proxy.get("pos_y", -999.0))))
		_expect(y == SURFACE_Y + 1,
				"proxy spawned away from surface: y=%d" % y)

	tick_system.free()
	if _failed:
		return
	print("Ecosystem proxy surface test passed: air chunks stay empty.")
	quit(0)


func _make_chunk(with_surface: bool) -> Dictionary:
	var cell_count := SIZE * SIZE * SIZE
	var materials := PackedByteArray()
	materials.resize(cell_count)
	materials.fill(0)

	var flags := PackedInt32Array()
	flags.resize(cell_count)

	if with_surface:
		for z in range(SIZE):
			for x in range(SIZE):
				var idx := _index_of(x, SURFACE_Y, z)
				materials[idx] = 1
				flags[idx] = TF_SOLID

	return {
		"size_x": SIZE,
		"size_y": SIZE,
		"size_z": SIZE,
		"materials": materials,
		"flags": flags,
	}


func _index_of(x: int, y: int, z: int) -> int:
	return (y * SIZE + z) * SIZE + x


func _expect(condition: bool, message: String) -> void:
	if condition:
		return
	_failed = true
	push_error("Ecosystem proxy surface test failed: " + message)
	quit(1)
