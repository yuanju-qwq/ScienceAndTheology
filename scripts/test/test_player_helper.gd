extends SceneTree

var _failed := false


func _init() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	_test_small_gravity_change_has_no_dead_zone()
	_test_compound_basis_uses_local_up_column()
	_test_running_across_chunks_has_no_angular_jump()
	if _failed:
		return
	print("Player helper test passed: gravity alignment is continuous and convergent.")
	quit(0)


func _test_small_gravity_change_has_no_dead_zone() -> void:
	var angle := deg_to_rad(0.5)
	var target_up := Vector3(sin(angle), cos(angle), 0.0)
	var result := GDPlayerHelper.align_body_to_gravity(
		Basis.IDENTITY, target_up, 8.0, 1.0 / 60.0)
	var before := Vector3.UP.angle_to(target_up)
	var after := result.y.normalized().angle_to(target_up)
	_expect(after < before * 0.1,
		"small gravity change remained in an angular dead zone: %.6f -> %.6f"
		% [before, after])


func _test_compound_basis_uses_local_up_column() -> void:
	var basis := Basis.from_euler(Vector3(0.4, 0.7, -0.2))
	var target_up := Vector3(0.3, 0.9, 0.2).normalized()
	var previous_error := basis.y.normalized().angle_to(target_up)
	for step in range(30):
		basis = GDPlayerHelper.align_body_to_gravity(
			basis, target_up, 8.0, 1.0 / 60.0)
		var error := basis.y.normalized().angle_to(target_up)
		_expect(error <= previous_error + 0.00001,
			"gravity alignment diverged at step %d: %.6f -> %.6f"
			% [step, previous_error, error])
		previous_error = error
	_expect(previous_error < 0.0001,
		"gravity alignment did not converge: %.6f" % previous_error)


func _test_running_across_chunks_has_no_angular_jump() -> void:
	const PLANET_RADIUS := 512.0
	const RUN_SPEED := 5.2 * 1.45
	const PHYSICS_DELTA := 1.0 / 60.0
	var basis := Basis.IDENTITY
	var position_x := 0.0
	var max_step_angle := 0.0
	while position_x < 64.0:
		position_x += RUN_SPEED * PHYSICS_DELTA
		var target_up := Vector3(position_x, PLANET_RADIUS, 0.0).normalized()
		var next_basis := GDPlayerHelper.align_body_to_gravity(
			basis, target_up, 8.0, PHYSICS_DELTA)
		max_step_angle = maxf(max_step_angle,
			basis.y.normalized().angle_to(next_basis.y.normalized()))
		basis = next_basis
	_expect(max_step_angle < deg_to_rad(0.1),
		"64-block run contained an angular jump: %.4f degrees"
		% rad_to_deg(max_step_angle))


func _expect(condition: bool, message: String) -> void:
	if condition or _failed:
		return
	_failed = true
	push_error("Player helper test failed: " + message)
	quit(1)
