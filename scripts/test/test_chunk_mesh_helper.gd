extends SceneTree


func _init() -> void:
	var transparent_mask := PackedByteArray()
	transparent_mask.resize(256)
	transparent_mask.fill(0)
	transparent_mask[4] = 1

	# Stone next to water must keep its shared face so the ground remains
	# visible through water. Water itself emits only its exposed top surface.
	var materials := PackedByteArray([1, 4])
	var mesh: Dictionary = GDChunkHelper.build_greedy_mesh(
			materials, 2, 1, 1, 0, 255, transparent_mask, {})
	if not _expect(mesh.has(1), "opaque material mesh is missing"):
		return
	if not _expect(mesh.has(4), "water surface mesh is missing"):
		return
	var stone_indices: PackedInt32Array = mesh[1].get(
			"indices", PackedInt32Array())
	var water_indices: PackedInt32Array = mesh[4].get(
			"indices", PackedInt32Array())
	if not _expect(stone_indices.size() == 36,
			"stone face beside water was culled"):
		return
	if not _expect(water_indices.size() == 6,
			"water generated internal or streaming-boundary walls"):
		return

	# A loaded same-material neighbor must suppress the chunk-boundary face.
	var neighbors := {2: PackedByteArray([4])}
	var boundary_mesh: Dictionary = GDChunkHelper.build_greedy_mesh(
			PackedByteArray([4]), 1, 1, 1, 0, 255,
			transparent_mask, neighbors)
	var boundary_indices: PackedInt32Array = boundary_mesh[4].get(
			"indices", PackedInt32Array())
	if not _expect(boundary_indices.size() == 6,
			"water face leaked across a loaded chunk boundary"):
		return

	print("Chunk mesh helper smoke passed: transparent faces and boundaries.")
	quit(0)


func _expect(condition: bool, message: String) -> bool:
	if condition:
		return true
	push_error("Chunk mesh helper smoke failed: " + message)
	quit(1)
	return false
