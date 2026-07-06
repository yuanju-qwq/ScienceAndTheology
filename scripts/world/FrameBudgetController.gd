# FrameBudgetController — unified frame-budget governor for streaming systems.
#
# Instead of letting each subsystem (chunk streaming, meshing, persistence
# restore, memory unload, LOD) guess its own per-frame budget, this node reads
# frame_ms p95 from RuntimePerfMonitor and drives all subsystem budgets through
# a single state machine:
#
#   Normal    : p95 < 18ms                 -> base budgets (cached at startup)
#   Pressure  : p95 > 25ms for >= 2s       -> heavy IO + mesh groups halved
#   Spike     : single frame > 33ms        -> background tasks capped at 1,
#               or p95 > 33ms                 gameplay section rebuilds exempt
#   Recovery  : p95 < 20ms for >= 3s       -> restore 25% of base per second
#
# Budgets are applied by direct setter on the ChunkRendererBridge (which is a
# PlanetShellAsyncChunkRendererBridge at runtime, inheriting every field below).
#
# Fields are split into three groups so pressure can target the real culprits
# (disk IO and mesh creation) without starving streaming latency:
#
#   heavy_io   : max_persistence_restores_per_frame
#                max_chunk_memory_unloads_per_frame
#                max_chunk_state_updates_per_frame
#   mesh       : max_chunk_views_per_frame
#                max_section_rebuilds_per_frame
#   streaming  : max_async_results_per_frame
#                max_chunk_load_requests_per_frame
#                horizontal_lod0_radius
class_name FrameBudgetController
extends Node

# --- State machine -----------------------------------------------------------

enum State {
	NORMAL,
	PRESSURE,
	SPIKE,
	RECOVERY,
}

# Thresholds (ms). Tuned per the second-phase design doc.
const NORMAL_P95_MS := 18.0
const PRESSURE_P95_MS := 25.0
const PRESSURE_HOLD_SECONDS := 2.0
const SPIKE_FRAME_MS := 33.0
const SPIKE_P95_MS := 33.0
const RECOVERY_P95_MS := 20.0
const RECOVERY_HOLD_SECONDS := 3.0

# Recovery restores 25% of the (base - current) gap per second.
const RECOVERY_RATE_PER_SECOND := 0.25
# Budget sampling is throttled to avoid querying the perf monitor every frame.
const SAMPLE_INTERVAL_SECONDS := 0.1

# Multiplier applied to each group when entering Pressure.
const PRESSURE_HEAVY_IO_SCALE := 0.5
const PRESSURE_MESH_SCALE := 0.5
const PRESSURE_STREAMING_SCALE := 1.0  # streaming latency must stay responsive

# Multipliers applied to each group when entering Spike.
const SPIKE_HEAVY_IO_SCALE := 0.25
const SPIKE_MESH_SCALE := 0.5  # chunk_views drops to 1 via floor, section rebuilds exempt
const SPIKE_STREAMING_SCALE := 0.5

# Floor for any per-frame budget. Never let a budget hit zero or streaming stalls.
const MIN_BACKGROUND_TASKS_PER_FRAME := 1

@export var chunk_bridge_path: NodePath = ^"../ChunkRendererBridge"
@export var perf_monitor_path: NodePath = ^"../RuntimePerfMonitor"
@export var enabled := true:
	set(value):
		enabled = value
		set_process(enabled)

var _chunk_bridge: ChunkRendererBridge = null
var _perf_monitor: RuntimePerfMonitor = null

# Current governor state. Exposed for PerfOverlay / debugging.
var current_state: State = State.NORMAL
var state_name: String:
	get:
		return State.keys()[current_state]

# Base budgets cached once on first apply. Reading @export defaults after the
# scene is ready gives the authoritative "intended" budget for Normal.
var _base_budgets: Dictionary = {}
var _base_cached := false

# Per-group multiplier currently applied. Drives both Pressure clamp and the
# Recovery ramp (lerped back toward 1.0 over time).
var _heavy_io_scale := 1.0
var _mesh_scale := 1.0
var _streaming_scale := 1.0

# Hysteresis timers for state transitions.
var _pressure_hold_seconds := 0.0
var _recovery_hold_seconds := 0.0
var _sample_elapsed := 0.0
var _recovery_accumulator := 0.0


func _ready() -> void:
	set_process(enabled)


func _process(delta: float) -> void:
	if not enabled:
		return
	if _chunk_bridge == null or _perf_monitor == null:
		_resolve_references()
		if _chunk_bridge == null or _perf_monitor == null:
			return

	# Cache base budgets on the first process tick. By then @export defaults
	# (or scene overrides) have been applied to the chunk bridge.
	if not _base_cached:
		_cache_base_budgets()
		_apply_budgets()

	_sample_elapsed += delta
	if _sample_elapsed < SAMPLE_INTERVAL_SECONDS:
		return
	var sample_delta := _sample_elapsed
	_sample_elapsed = 0.0

	var frame_stats: Dictionary = _perf_monitor.get_frame_stats()
	var p95_ms := float(frame_stats.get("p95_ms", 0.0))
	var last_ms := float(frame_stats.get("last_ms", 0.0))

	_update_state(delta, sample_delta, p95_ms, last_ms)
	_apply_budgets()


# --- Reference resolution ----------------------------------------------------

func _resolve_references() -> void:
	if _chunk_bridge == null:
		_chunk_bridge = get_node_or_null(chunk_bridge_path) as ChunkRendererBridge
	if _perf_monitor == null:
		_perf_monitor = get_node_or_null(perf_monitor_path) as RuntimePerfMonitor


# --- Base budget cache -------------------------------------------------------

# Snapshot the chunk bridge's current field values. These are the "intended"
# budgets the operator (designer / scene author) configured; Pressure/Spike
# scale down from here and Recovery ramps back up to here.
func _cache_base_budgets() -> void:
	if _chunk_bridge == null:
		return
	_base_budgets = {
		"max_async_results_per_frame": _chunk_bridge.max_async_results_per_frame,
		"max_chunk_load_requests_per_frame": _chunk_bridge.max_chunk_load_requests_per_frame,
		"max_chunk_views_per_frame": _chunk_bridge.max_chunk_views_per_frame,
		"max_section_rebuilds_per_frame": _chunk_bridge.max_section_rebuilds_per_frame,
		"max_persistence_restores_per_frame": _chunk_bridge.max_persistence_restores_per_frame,
		"max_chunk_state_updates_per_frame": _chunk_bridge.max_chunk_state_updates_per_frame,
		"max_chunk_memory_unloads_per_frame": _chunk_bridge.max_chunk_memory_unloads_per_frame,
		"horizontal_lod0_radius": _chunk_bridge.horizontal_lod0_radius,
	}
	_base_cached = true


# --- State machine -----------------------------------------------------------

func _update_state(delta: float, sample_delta: float, p95_ms: float, last_ms: float) -> void:
	# A single bad frame triggers Spike immediately, regardless of p95.
	var spike_now := last_ms >= SPIKE_FRAME_MS or p95_ms >= SPIKE_P95_MS
	var pressure_now := p95_ms >= PRESSURE_P95_MS
	var recovery_now := p95_ms <= RECOVERY_P95_MS

	match current_state:
		State.NORMAL:
			if spike_now:
				_enter_state(State.SPIKE)
			elif pressure_now:
				_pressure_hold_seconds += sample_delta
				if _pressure_hold_seconds >= PRESSURE_HOLD_SECONDS:
					_enter_state(State.PRESSURE)
			else:
				_pressure_hold_seconds = 0.0

		State.PRESSURE:
			if spike_now:
				_enter_state(State.SPIKE)
			elif recovery_now:
				# Pressure cleared: drift toward Recovery only when sustained.
				_recovery_hold_seconds += sample_delta
				if _recovery_hold_seconds >= RECOVERY_HOLD_SECONDS:
					_enter_state(State.RECOVERY)
			else:
				_recovery_hold_seconds = 0.0

		State.SPIKE:
			# Stay in Spike until p95 is back under control.
			if not spike_now and p95_ms < PRESSURE_P95_MS:
				# Spike recovered directly to Normal-ish: drift through Recovery
				# so budgets ramp up gradually rather than snapping back.
				_enter_state(State.RECOVERY)
			elif not spike_now and p95_ms < SPIKE_P95_MS:
				_enter_state(State.PRESSURE)

		State.RECOVERY:
			if spike_now:
				_enter_state(State.SPIKE)
			elif pressure_now:
				# Pressure returned mid-recovery: snap back to Pressure immediately.
				_enter_state(State.PRESSURE)
			else:
				# Ramp scales toward 1.0; once everything is fully restored,
				# transition back to Normal.
				_recovery_accumulator += delta
				if _recovery_accumulator >= 1.0:
					var steps := int(_recovery_accumulator)
					_recovery_accumulator -= float(steps)
					_ramp_recovery(steps)
				if _is_fully_recovered():
					_enter_state(State.NORMAL)


func _enter_state(state: State) -> void:
	current_state = state
	_pressure_hold_seconds = 0.0
	_recovery_hold_seconds = 0.0
	_recovery_accumulator = 0.0
	match state:
		State.NORMAL:
			_heavy_io_scale = 1.0
			_mesh_scale = 1.0
			_streaming_scale = 1.0
		State.PRESSURE:
			_heavy_io_scale = PRESSURE_HEAVY_IO_SCALE
			_mesh_scale = PRESSURE_MESH_SCALE
			_streaming_scale = PRESSURE_STREAMING_SCALE
		State.SPIKE:
			_heavy_io_scale = SPIKE_HEAVY_IO_SCALE
			_mesh_scale = SPIKE_MESH_SCALE
			_streaming_scale = SPIKE_STREAMING_SCALE
		State.RECOVERY:
			# Scales remain at their current (reduced) values; _ramp_recovery
			# nudges them toward 1.0 each second.
			pass


# Nudge each group scale toward 1.0 by RECOVERY_RATE_PER_SECOND per step.
func _ramp_recovery(steps: int) -> void:
	if steps <= 0:
		return
	var step_rate := RECOVERY_RATE_PER_SECOND * float(steps)
	_heavy_io_scale = minf(1.0, _heavy_io_scale + step_rate)
	_mesh_scale = minf(1.0, _mesh_scale + step_rate)
	_streaming_scale = minf(1.0, _streaming_scale + step_rate)


func _is_fully_recovered() -> bool:
	return is_equal_approx(_heavy_io_scale, 1.0) \
			and is_equal_approx(_mesh_scale, 1.0) \
			and is_equal_approx(_streaming_scale, 1.0)


# --- Budget application ------------------------------------------------------

# Push the current per-group scales into the chunk bridge's @export fields.
# Integer fields (chunk views, requests, lod radius) are floored to the
# MIN_BACKGROUND_TASKS_PER_FRAME floor so streaming never fully stalls.
func _apply_budgets() -> void:
	if _chunk_bridge == null or not _base_cached:
		return

	# heavy_io group
	_chunk_bridge.max_persistence_restores_per_frame = _scaled_int(
			"max_persistence_restores_per_frame", _heavy_io_scale)
	_chunk_bridge.max_chunk_memory_unloads_per_frame = _scaled_int(
			"max_chunk_memory_unloads_per_frame", _heavy_io_scale)
	_chunk_bridge.max_chunk_state_updates_per_frame = _scaled_int(
			"max_chunk_state_updates_per_frame", _heavy_io_scale)

	# mesh group.
	# NOTE: section rebuilds serve gameplay edits (mining/placing). They are
	# exempt from Spike pressure so player edits never queue behind streaming.
	_chunk_bridge.max_chunk_views_per_frame = _scaled_int(
			"max_chunk_views_per_frame", _mesh_scale)
	_chunk_bridge.max_section_rebuilds_per_frame = _base_budgets.get(
			"max_section_rebuilds_per_frame", _chunk_bridge.max_section_rebuilds_per_frame)

	# streaming group
	_chunk_bridge.max_async_results_per_frame = _scaled_int(
			"max_async_results_per_frame", _streaming_scale)
	_chunk_bridge.max_chunk_load_requests_per_frame = _scaled_int(
			"max_chunk_load_requests_per_frame", _streaming_scale)
	_chunk_bridge.horizontal_lod0_radius = _scaled_int(
			"horizontal_lod0_radius", _streaming_scale)


# Scale a base integer budget by a multiplier, floored to the minimum.
# Returns at least MIN_BACKGROUND_TASKS_PER_FRAME so a budget never reaches 0.
func _scaled_int(key: String, scale: float) -> int:
	var base := int(_base_budgets.get(key, 0))
	if base <= 0:
		return base
	var scaled := int(round(float(base) * scale))
	return maxi(MIN_BACKGROUND_TASKS_PER_FRAME, scaled)


# --- Debug / introspection ---------------------------------------------------

# Expose current governor snapshot for PerfOverlay / console.
func get_governor_stats() -> Dictionary:
	return {
		"state": state_name,
		"heavy_io_scale": _heavy_io_scale,
		"mesh_scale": _mesh_scale,
		"streaming_scale": _streaming_scale,
		"pressure_hold_seconds": _pressure_hold_seconds,
		"recovery_hold_seconds": _recovery_hold_seconds,
		"base_budgets": _base_budgets.duplicate(),
	}
