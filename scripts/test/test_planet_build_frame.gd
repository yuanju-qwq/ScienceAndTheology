extends SceneTree

var _failed := false


func _init() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	var center := Vector3(0.0, -512.0, 0.0)
	var north := Vector3i(0, 0, 0)
	_expect(GDPlanetBuildFrame.local_up(north, center) == Vector3i.UP,
		"north surface local up did not resolve to +Y")
	_expect(GDPlanetBuildFrame.local_down(north, center) == Vector3i.DOWN,
		"north surface local down did not resolve to -Y")
	_expect(GDPlanetBuildFrame.local_horizontal(
		north, center, Vector3(1.0, 1.0, 0.0)) == Vector3i.RIGHT,
		"north surface tangent direction did not remove radial Y")

	var east := Vector3i(512, -512, 0)
	_expect(GDPlanetBuildFrame.local_up(east, center) == Vector3i.RIGHT,
		"east surface local up did not resolve to +X")
	var east_horizontal := GDPlanetBuildFrame.local_horizontal(
		east, center, Vector3.RIGHT)
	_expect(GDPlanetBuildFrame.is_axis_direction(east_horizontal),
		"degenerate tangent request did not return an axis neighbor")
	_expect(east_horizontal != Vector3i.RIGHT and east_horizontal != Vector3i.LEFT,
		"degenerate tangent request returned local up/down")

	_expect(GDPlanetBuildFrame.classify_direction(
		east, center, Vector3i.RIGHT) == GDPlanetBuildFrame.LOCAL_DIRECTION_UP,
		"east +X was not classified as local up")
	_expect(GDPlanetBuildFrame.classify_direction(
		east, center, Vector3i.UP) == GDPlanetBuildFrame.LOCAL_DIRECTION_HORIZONTAL,
		"east +Y was not classified as horizontal")
	_expect(GDPlanetBuildFrame.snap_global_axis(
		Vector3(-0.8, 0.1, 0.2)) == Vector3i.LEFT,
		"global axis snapping selected the wrong signed component")
	_expect(not GDPlanetBuildFrame.is_axis_direction(Vector3i(1, 1, 0)),
		"diagonal direction was accepted as a voxel neighbor")

	if _failed:
		return
	print("Planet build frame test passed: local radial/tangent directions are stable.")
	quit(0)


func _expect(condition: bool, message: String) -> void:
	if condition or _failed:
		return
	_failed = true
	push_error("Planet build frame test failed: " + message)
	quit(1)
