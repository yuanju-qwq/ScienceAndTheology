# Opt-in U0 prototype performance sampler.
# Enable with SNT_U0_BASELINE=1. Normal gameplay does no sampling or logging.
class_name PrototypeBaselineMonitor
extends Node

@export var enabled := false
@export var sample_interval_seconds := 5.0
@export var capture_duration_seconds := 60.0
@export var output_path := "user://u0_prototype_baseline.json"
@export var chunk_bridge_path: NodePath = ^"../ChunkRendererBridge"
@export var universe_manager_path: NodePath = ^"../UniverseManager"

var _elapsed := 0.0
var _sample_elapsed := 0.0
var _samples: Array[Dictionary] = []
var _chunk_bridge: ChunkRendererBridge = null
var _universe_manager: UniverseManager = null
var _finished := false


func _ready() -> void:
	enabled = enabled or OS.get_environment("SNT_U0_BASELINE") == "1"
	if not enabled:
		set_process(false)
		return

	var duration_value := OS.get_environment("SNT_U0_BASELINE_SECONDS")
	if duration_value.is_valid_float():
		capture_duration_seconds = maxf(1.0, duration_value.to_float())
	var configured_output := OS.get_environment("SNT_U0_BASELINE_OUTPUT")
	if configured_output != "":
		output_path = configured_output

	_chunk_bridge = get_node_or_null(chunk_bridge_path) as ChunkRendererBridge
	_universe_manager = get_node_or_null(universe_manager_path) as UniverseManager
	print("[U0Baseline] capture started: interval=%.1fs duration=%.1fs output=%s" % [
		sample_interval_seconds, capture_duration_seconds, output_path])


func _process(delta: float) -> void:
	if _finished:
		return
	_elapsed += delta
	_sample_elapsed += delta
	if _sample_elapsed >= sample_interval_seconds:
		_sample_elapsed = 0.0
		_capture_sample()
	if _elapsed >= capture_duration_seconds:
		_finish_capture()


func _capture_sample() -> void:
	var sample: Dictionary = {
		"elapsed_seconds": _elapsed,
		"fps": Engine.get_frames_per_second(),
		"static_memory_bytes": int(Performance.get_monitor(Performance.MEMORY_STATIC)),
		"node_count": int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT)),
	}
	if _chunk_bridge != null:
		sample["chunk_streaming"] = _chunk_bridge.get_streaming_metrics()
	if _universe_manager != null:
		sample["universe"] = _universe_manager.get_runtime_metrics()
		var save_manager := _universe_manager.get_save_manager()
		if save_manager != null:
			sample["save_io"] = save_manager.get_io_metrics()
	_samples.append(sample)
	_flush_report()
	print("[U0Baseline] sample=%d elapsed=%.1fs memory_mb=%.1f visible=%d queued=%d" % [
		_samples.size(), _elapsed,
		float(sample["static_memory_bytes"]) / (1024.0 * 1024.0),
		_get_chunk_metric(sample, "visible_chunks"),
		_get_chunk_metric(sample, "queued_chunk_views")])


func _get_chunk_metric(sample: Dictionary, key: String) -> int:
	var streaming: Dictionary = sample.get("chunk_streaming", {})
	return int(streaming.get(key, 0))


func _finish_capture() -> void:
	_finished = true
	if _samples.is_empty() or _samples.back().get("elapsed_seconds", 0.0) < _elapsed:
		_capture_sample()
	var saved := _flush_report()
	print("[U0Baseline] capture complete: samples=%d saved=%s output=%s" % [
		_samples.size(), str(saved), output_path])
	if OS.get_environment("SNT_U0_BASELINE_QUIT") == "1":
		get_tree().quit(0 if saved else 1)


func _flush_report() -> bool:
	var resolved_path := ProjectSettings.globalize_path(output_path)
	var output_dir := resolved_path.get_base_dir()
	if output_dir != "":
		var dir_error := DirAccess.make_dir_recursive_absolute(output_dir)
		if dir_error != OK and dir_error != ERR_ALREADY_EXISTS:
			push_warning("U0Baseline: failed to create output directory: %s" % output_dir)
			return false
	var file := FileAccess.open(resolved_path, FileAccess.WRITE)
	if file == null:
		push_warning("U0Baseline: failed to open output: %s" % resolved_path)
		return false
	var report := {
		"schema_version": 1,
		"project": "ScienceAndTheology",
		"capture_started_unix": Time.get_unix_time_from_system() - _elapsed,
		"sample_interval_seconds": sample_interval_seconds,
		"capture_duration_seconds": capture_duration_seconds,
		"samples": _samples,
	}
	file.store_string(JSON.stringify(report, "\t"))
	file.close()
	return true
