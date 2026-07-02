# RuntimePerfMonitor keeps frame spike diagnostics cheap during normal play.
# It samples frame time every frame, but only prints when a spike passes a
# threshold and the relevant log cooldown has expired.
class_name RuntimePerfMonitor
extends Node

@export var enabled := true:
	set(value):
		enabled = value
		set_process(enabled)

@export var spike_frame_ms := 33.0
@export var serious_spike_frame_ms := 50.0
@export var spike_log_cooldown_seconds := 5.0
@export var serious_log_cooldown_seconds := 3.0
@export var warmup_seconds := 2.0
@export var frame_window_seconds := 10.0
@export var expected_max_fps := 120
@export var max_spike_events := 64
@export var chunk_bridge_path: NodePath = ^"../ChunkRendererBridge"
@export var universe_manager_path: NodePath = ^"../UniverseManager"
@export var tick_system_path: NodePath = ^"../GDTickSystem"

var _chunk_bridge: Node = null
var _universe_manager: Node = null
var _tick_system: Node = null

var _frame_samples := PackedFloat32Array()
var _frame_sample_index := 0
var _frame_sample_count := 0
var _spike_events: Array[Dictionary] = []

var _last_spike_log_usec := 0
var _last_serious_log_usec := 0
var _spikes_since_log := 0
var _serious_spikes_since_log := 0
var _max_spike_since_log := 0.0
var _total_spikes := 0
var _total_serious_spikes := 0
var _last_frame_ms := 0.0
var _elapsed_seconds := 0.0


func _ready() -> void:
	_resize_frame_window()
	_resolve_references()
	set_process(enabled)


func _process(delta: float) -> void:
	if not enabled:
		return
	if _chunk_bridge == null or _universe_manager == null or _tick_system == null:
		_resolve_references()

	var frame_ms := delta * 1000.0
	_last_frame_ms = frame_ms
	_elapsed_seconds += delta
	_record_frame_sample(frame_ms)

	if _elapsed_seconds < warmup_seconds:
		return

	if frame_ms >= serious_spike_frame_ms:
		_record_spike(frame_ms, true)
	elif frame_ms >= spike_frame_ms:
		_record_spike(frame_ms, false)


func _resize_frame_window() -> void:
	var size := maxi(1, int(ceil(frame_window_seconds * float(expected_max_fps))))
	_frame_samples.resize(size)
	_frame_sample_index = 0
	_frame_sample_count = 0


func _resolve_references() -> void:
	if _chunk_bridge == null:
		_chunk_bridge = get_node_or_null(chunk_bridge_path)
	if _universe_manager == null:
		_universe_manager = get_node_or_null(universe_manager_path)
	if _tick_system == null:
		_tick_system = get_node_or_null(tick_system_path)


func _record_frame_sample(frame_ms: float) -> void:
	if _frame_samples.is_empty():
		_resize_frame_window()
	_frame_samples[_frame_sample_index] = frame_ms
	_frame_sample_index = (_frame_sample_index + 1) % _frame_samples.size()
	_frame_sample_count = mini(_frame_sample_count + 1, _frame_samples.size())


func _record_spike(frame_ms: float, serious: bool) -> void:
	_total_spikes += 1
	_spikes_since_log += 1
	_max_spike_since_log = maxf(_max_spike_since_log, frame_ms)
	if serious:
		_total_serious_spikes += 1
		_serious_spikes_since_log += 1

	var event := _build_spike_event(frame_ms, serious)
	_spike_events.append(event)
	while _spike_events.size() > max_spike_events:
		_spike_events.pop_front()

	var now_usec := Time.get_ticks_usec()
	if serious:
		var serious_cooldown_usec := int(serious_log_cooldown_seconds * 1000000.0)
		if now_usec - _last_serious_log_usec >= serious_cooldown_usec:
			_print_spike_summary(true, event)
			_last_serious_log_usec = now_usec
			_last_spike_log_usec = now_usec
			return

	var spike_cooldown_usec := int(spike_log_cooldown_seconds * 1000000.0)
	if now_usec - _last_spike_log_usec >= spike_cooldown_usec:
		_print_spike_summary(false, event)
		_last_spike_log_usec = now_usec


func _build_spike_event(frame_ms: float, serious: bool) -> Dictionary:
	var streaming := _get_streaming_metrics()
	return {
		"time_msec": Time.get_ticks_msec(),
		"frame_ms": frame_ms,
		"serious": serious,
		"fps": Engine.get_frames_per_second(),
		"node_count": int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT)),
		"static_memory_mb": (
				float(Performance.get_monitor(Performance.MEMORY_STATIC))
				/ (1024.0 * 1024.0)),
		"visible_chunks": int(streaming.get("visible_chunks", 0)),
		"queued_chunk_views": int(streaming.get("queued_chunk_views", 0)),
		"queued_chunk_rebuilds": int(streaming.get("queued_chunk_rebuilds", 0)),
		"async_generation_pending": int(streaming.get("async_generation_pending", 0)),
		"mesh_build_last_ms": float(streaming.get("mesh_build_last_ms", 0.0)),
		"mesh_build_max_ms": float(streaming.get("mesh_build_max_ms", 0.0)),
		"persistence_restore_queue_size": int(
				streaming.get("persistence_restore_queue_size", 0)),
		"persistence_restores_processed": int(
				streaming.get("persistence_restores_processed", 0)),
		"memory_unloaded_chunks": int(streaming.get("memory_unloaded_chunks", 0)),
		"active_chunks": _get_tick_active_chunk_count(),
		"tick_count": _get_tick_count(),
	}


func _print_spike_summary(serious: bool, event: Dictionary) -> void:
	var level := "serious" if serious else "spike"
	var prefix := "[FramePerf:%s]" % level
	var format := (
			"%s frame_ms=%.2f spikes=%d serious=%d max_since_log=%.2f "
			+ "fps=%d nodes=%d visible=%d queued=%d async=%d "
			+ "mesh_last=%.2f mesh_max=%.2f restore_q=%d active=%d")
	print(format % [
		prefix,
		float(event.get("frame_ms", 0.0)),
		_spikes_since_log,
		_serious_spikes_since_log,
		_max_spike_since_log,
		int(event.get("fps", 0)),
		int(event.get("node_count", 0)),
		int(event.get("visible_chunks", 0)),
		int(event.get("queued_chunk_views", 0)),
		int(event.get("async_generation_pending", 0)),
		float(event.get("mesh_build_last_ms", 0.0)),
		float(event.get("mesh_build_max_ms", 0.0)),
		int(event.get("persistence_restore_queue_size", 0)),
		int(event.get("active_chunks", 0)),
	])
	_spikes_since_log = 0
	_serious_spikes_since_log = 0
	_max_spike_since_log = 0.0


func _get_streaming_metrics() -> Dictionary:
	if _chunk_bridge == null or not _chunk_bridge.has_method("get_streaming_metrics"):
		return {}
	var metrics: Variant = _chunk_bridge.call("get_streaming_metrics")
	if metrics is Dictionary:
		return metrics
	return {}


func _get_tick_active_chunk_count() -> int:
	if _tick_system == null or not _tick_system.has_method("get_active_chunk_count"):
		return 0
	return int(_tick_system.call("get_active_chunk_count"))


func _get_tick_count() -> int:
	if _tick_system == null or not _tick_system.has_method("get_tick_count"):
		return 0
	return int(_tick_system.call("get_tick_count"))


func get_frame_stats() -> Dictionary:
	if _frame_sample_count <= 0:
		return {
			"last_ms": _last_frame_ms,
			"avg_ms": 0.0,
			"max_ms": 0.0,
			"p95_ms": 0.0,
			"samples": 0,
			"total_spikes": _total_spikes,
			"total_serious_spikes": _total_serious_spikes,
		}

	var values: Array[float] = []
	values.resize(_frame_sample_count)
	var total := 0.0
	var max_ms := 0.0
	for i in range(_frame_sample_count):
		var value := float(_frame_samples[i])
		values[i] = value
		total += value
		max_ms = maxf(max_ms, value)
	values.sort()
	var p95_index := clampi(
			int(ceil(float(values.size()) * 0.95)) - 1, 0, values.size() - 1)
	return {
		"last_ms": _last_frame_ms,
		"avg_ms": total / float(_frame_sample_count),
		"max_ms": max_ms,
		"p95_ms": values[p95_index],
		"samples": _frame_sample_count,
		"total_spikes": _total_spikes,
		"total_serious_spikes": _total_serious_spikes,
	}


func get_recent_spikes(limit: int = 8) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var count := clampi(limit, 0, _spike_events.size())
	var start := _spike_events.size() - count
	for i in range(start, _spike_events.size()):
		result.append(_spike_events[i])
	return result


func get_spike_report(limit: int = 8) -> PackedStringArray:
	var lines := PackedStringArray()
	var frame_stats := get_frame_stats()
	lines.append("Frame: last=%.2fms avg=%.2fms p95=%.2fms max=%.2fms spikes=%d serious=%d" % [
		float(frame_stats.get("last_ms", 0.0)),
		float(frame_stats.get("avg_ms", 0.0)),
		float(frame_stats.get("p95_ms", 0.0)),
		float(frame_stats.get("max_ms", 0.0)),
		int(frame_stats.get("total_spikes", 0)),
		int(frame_stats.get("total_serious_spikes", 0)),
	])
	var events := get_recent_spikes(limit)
	if events.is_empty():
		lines.append("No frame spikes recorded.")
		return lines
	for event in events:
		lines.append("%sms frame=%.2f fps=%d visible=%d queued=%d async=%d mesh_last=%.2f mesh_max=%.2f restore_q=%d active=%d" % [
			"!" if bool(event.get("serious", false)) else " ",
			float(event.get("frame_ms", 0.0)),
			int(event.get("fps", 0)),
			int(event.get("visible_chunks", 0)),
			int(event.get("queued_chunk_views", 0)),
			int(event.get("async_generation_pending", 0)),
			float(event.get("mesh_build_last_ms", 0.0)),
			float(event.get("mesh_build_max_ms", 0.0)),
			int(event.get("persistence_restore_queue_size", 0)),
			int(event.get("active_chunks", 0)),
		])
	return lines
