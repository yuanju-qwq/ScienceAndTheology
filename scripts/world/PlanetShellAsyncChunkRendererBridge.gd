# PlanetShellAsyncChunkRendererBridge — small scheduling layer over
# PlanetShellChunkRendererBridge.
#
# The base shell bridge already knows how to save-before-remove and how to load a
# single chunk from the region file. This subclass prevents restore bursts from
# happening inside the chunk-selection loop: missing chunks are queued, sorted by
# distance to the player chunk, and restored from persistence with a per-frame
# budget. If no saved chunk exists, the normal async generation path is used.
class_name PlanetShellAsyncChunkRendererBridge
extends PlanetShellChunkRendererBridge

@export var persistence_restore_queue_enabled := true
# NOTE: max_persistence_restores_per_frame is declared on the base ChunkRendererBridge.
@export var max_persistence_restore_queue_size := 2048
@export var fallback_to_async_generation_on_restore_miss := true

var _persistence_restore_queue: Array[Vector3i] = []
var _persistence_restore_queued: Dictionary = {}
var _current_player_chunk := Vector3i.ZERO

var _last_persistence_restores_processed := 0
var _last_persistence_restore_hits := 0
var _last_persistence_restore_misses := 0
var _last_persistence_restore_fallbacks := 0
var _last_persistence_restore_dropped := 0


func get_streaming_metrics() -> Dictionary:
	var metrics := super.get_streaming_metrics()
	metrics["persistence_restore_queue_enabled"] = persistence_restore_queue_enabled
	metrics["persistence_restore_queue_size"] = _persistence_restore_queue.size()
	metrics["persistence_restores_processed"] = _last_persistence_restores_processed
	metrics["persistence_restore_hits"] = _last_persistence_restore_hits
	metrics["persistence_restore_misses"] = _last_persistence_restore_misses
	metrics["persistence_restore_fallbacks"] = _last_persistence_restore_fallbacks
	metrics["persistence_restore_dropped"] = _last_persistence_restore_dropped
	return metrics


func set_active_dimension(dimension_id: StringName) -> void:
	_persistence_restore_queue.clear()
	_persistence_restore_queued.clear()
	super.set_active_dimension(dimension_id)


func _refresh_chunks(player_chunk: Vector3i) -> void:
	_current_player_chunk = player_chunk
	_last_persistence_restores_processed = 0
	_last_persistence_restore_hits = 0
	_last_persistence_restore_misses = 0
	_last_persistence_restore_fallbacks = 0
	_last_persistence_restore_dropped = 0

	super._refresh_chunks(player_chunk)
	_process_persistence_restore_queue()


func _ensure_chunk_loaded(chunk: Vector3i) -> void:
	if world_data == null:
		return
	if _tracked_chunks.has(chunk):
		return
	if world_data.has_chunk(active_dimension, chunk.x, chunk.y, chunk.z):
		_tracked_chunks[chunk] = true
		return
	if _should_queue_persistence_restore(chunk):
		_queue_persistence_restore(chunk)
		return
	_request_async_generation(chunk)


func _should_queue_persistence_restore(chunk: Vector3i) -> bool:
	if not persistence_restore_queue_enabled:
		return false
	if _get_chunk_persistence_helper() == null:
		return false
	if _get_universe_save_dir() == "":
		return false
	if _persistence_restore_queued.has(chunk):
		return true
	return true


func _queue_persistence_restore(chunk: Vector3i) -> void:
	if _persistence_restore_queued.has(chunk):
		return
	if max_persistence_restore_queue_size > 0 \
			and _persistence_restore_queue.size() >= max_persistence_restore_queue_size:
		_drop_farthest_restore_candidate()
		if _persistence_restore_queue.size() >= max_persistence_restore_queue_size:
			_last_persistence_restore_dropped += 1
			_request_async_generation(chunk)
			return
	_persistence_restore_queue.append(chunk)
	_persistence_restore_queued[chunk] = true


func _drop_farthest_restore_candidate() -> void:
	if _persistence_restore_queue.is_empty():
		return
	var farthest_index := 0
	var farthest_distance := -1
	for i in range(_persistence_restore_queue.size()):
		var chunk := _persistence_restore_queue[i]
		var distance := _chunk_queue_distance(chunk, _current_player_chunk)
		if distance > farthest_distance:
			farthest_distance = distance
			farthest_index = i
	var removed := _persistence_restore_queue[farthest_index]
	_persistence_restore_queue.remove_at(farthest_index)
	_persistence_restore_queued.erase(removed)
	_last_persistence_restore_dropped += 1


func _process_persistence_restore_queue() -> void:
	if not persistence_restore_queue_enabled:
		return
	if _persistence_restore_queue.is_empty():
		return
	_sort_persistence_restore_queue()
	var processed := 0
	while not _persistence_restore_queue.is_empty():
		if max_persistence_restores_per_frame > 0 \
				and processed >= max_persistence_restores_per_frame:
			return
		var chunk: Vector3i = _persistence_restore_queue.pop_front()
		_persistence_restore_queued.erase(chunk)
		if world_data == null:
			return
		if world_data.has_chunk(active_dimension, chunk.x, chunk.y, chunk.z):
			_tracked_chunks[chunk] = true
			continue
		if world_data.is_chunk_async_pending(active_dimension, chunk.x, chunk.y, chunk.z):
			continue
		processed += 1
		_last_persistence_restores_processed += 1
		if _try_restore_chunk_from_persistence(chunk):
			_last_persistence_restore_hits += 1
			continue
		_last_persistence_restore_misses += 1
		if fallback_to_async_generation_on_restore_miss:
			_request_async_generation(chunk)
			_last_persistence_restore_fallbacks += 1


func _sort_persistence_restore_queue() -> void:
	_persistence_restore_queue.sort_custom(func(a: Vector3i, b: Vector3i) -> bool:
		return _chunk_queue_distance(a, _current_player_chunk) \
				< _chunk_queue_distance(b, _current_player_chunk))


func _request_async_generation(chunk: Vector3i) -> void:
	if world_data == null:
		return
	if world_data.has_chunk(active_dimension, chunk.x, chunk.y, chunk.z):
		_tracked_chunks[chunk] = true
		return
	if world_data.is_chunk_async_pending(active_dimension, chunk.x, chunk.y, chunk.z):
		return
	if max_chunk_load_requests_per_frame > 0 \
			and _chunk_request_count_this_frame >= max_chunk_load_requests_per_frame:
		return
	world_data.request_chunk_async(active_dimension, chunk.x, chunk.y, chunk.z)
	_chunk_request_count_this_frame += 1


func _chunk_queue_distance(a: Vector3i, b: Vector3i) -> int:
	return maxi(maxi(abs(a.x - b.x), abs(a.y - b.y)), abs(a.z - b.z))
