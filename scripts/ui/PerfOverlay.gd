# PerfOverlay — 实时性能监控覆盖层，显示 TPS 和网络数据传输量。
# 通过 /perf 控制台命令切换显示。
class_name PerfOverlay
extends Control

# 每个 dirty chunk 的估算数据量（字节），包含地形变更 + 机器状态。
const ESTIMATED_BYTES_PER_DIRTY_CHUNK := 4096

var _label: Label
var _bg: ColorRect
var _tick_system: GDTickSystem = null
var _chunk_bridge: ChunkRendererBridge = null
var _perf_monitor: Node = null
# FrameBudgetController reference for governor state display (Phase 5).
var _budget_controller: FrameBudgetController = null

# TPS 测量
var _last_tick_count := 0
var _tick_sample_elapsed := 0.0
var _measured_tps := 0.0

# 网络数据传输量测量
var _last_dirty_count := 0
var _net_sample_elapsed := 0.0
var _dirty_chunks_per_sec := 0.0
var _estimated_bytes_per_sec := 0.0

# 更新间隔（秒），避免每帧刷新文字造成性能开销。
var _update_interval := 0.5
var _update_elapsed := 0.0


func _ready() -> void:
	visible = false
	set_process(false)
	_build_ui()


func _process(delta: float) -> void:
	# 测量 TPS：累计 tick 计数器差值。
	if _tick_system != null:
		_tick_sample_elapsed += delta
		var current_tick := _tick_system.get_tick_count()
		if _tick_sample_elapsed >= 1.0:
			_measured_tps = float(current_tick - _last_tick_count) / _tick_sample_elapsed
			_last_tick_count = current_tick
			_tick_sample_elapsed = 0.0

	# 测量网络数据传输量：跟踪 dirty chunks 变化率。
	if _tick_system != null:
		_net_sample_elapsed += delta
		var current_dirty := _tick_system.get_dirty_chunks().size()
		if _net_sample_elapsed >= 1.0:
			var dirty_delta := current_dirty - _last_dirty_count
			# dirty_delta 可能为负（chunks 被清理），取绝对值。
			_dirty_chunks_per_sec = float(abs(dirty_delta)) / _net_sample_elapsed
			_estimated_bytes_per_sec = _dirty_chunks_per_sec * ESTIMATED_BYTES_PER_DIRTY_CHUNK
			_last_dirty_count = current_dirty
			_net_sample_elapsed = 0.0

	# 按间隔刷新显示文字。
	_update_elapsed += delta
	if _update_elapsed < _update_interval:
		return
	_update_elapsed = 0.0
	_refresh_text()


func setup(tick_system: GDTickSystem, chunk_bridge: ChunkRendererBridge,
		perf_monitor: Node = null,
		budget_controller: FrameBudgetController = null) -> void:
	_tick_system = tick_system
	_chunk_bridge = chunk_bridge
	_perf_monitor = perf_monitor
	_budget_controller = budget_controller
	if _tick_system != null:
		_last_tick_count = _tick_system.get_tick_count()
		_last_dirty_count = _tick_system.get_dirty_chunks().size()


func toggle() -> void:
	visible = not visible
	set_process(visible)
	if visible and _tick_system != null:
		_last_tick_count = _tick_system.get_tick_count()
		_last_dirty_count = _tick_system.get_dirty_chunks().size()
		_tick_sample_elapsed = 0.0
		_net_sample_elapsed = 0.0
		_measured_tps = 0.0
		_dirty_chunks_per_sec = 0.0
		_estimated_bytes_per_sec = 0.0
		_update_elapsed = _update_interval  # 立即刷新一次


func _build_ui() -> void:
	# 半透明深色背景，位于 LayerStatusLabel 下方。
	_bg = ColorRect.new()
	_bg.name = "PerfBackground"
	_bg.offset_left = 12.0
	_bg.offset_top = 64.0
	_bg.offset_right = 268.0
	_bg.offset_bottom = 116.0
	_bg.color = Color(0.035, 0.04, 0.045, 0.72)
	add_child(_bg)

	_label = Label.new()
	_label.name = "PerfLabel"
	_label.offset_left = 20.0
	_label.offset_top = 68.0
	_label.offset_right = 260.0
	_label.offset_bottom = 112.0
	_label.add_theme_font_size_override("font_size", 13)
	_label.text = "TPS: -- | FPS: --\nNet: --"
	add_child(_label)


func _refresh_text() -> void:
	var fps := Engine.get_frames_per_second()
	var tps_text := "TPS: %.1f | FPS: %d" % [_measured_tps, fps]

	# 网络数据传输量。
	var net_text := "Net: %s/s" % _format_bytes(_estimated_bytes_per_sec)

	# 附加 streaming 指标。
	var streaming_text := ""
	if _chunk_bridge != null:
		var metrics := _chunk_bridge.get_streaming_metrics()
		var visible_chunks: int = metrics.get("visible_chunks", 0)
		var active_chunks := 0
		if _tick_system != null:
			active_chunks = _tick_system.get_active_chunk_count()
		var dirty_chunks := 0
		if _tick_system != null:
			dirty_chunks = _tick_system.get_dirty_chunks().size()
		net_text += " | Chunks: %d/%d dirty:%d" % [visible_chunks, active_chunks, dirty_chunks]

		# Detailed mesh/streaming stats (Phase 5): build count, average build
		# time, queued views, tracked chunks, and async pending requests.
		streaming_text = "Mesh: %d avg:%.2fms queued:%d tracked:%d pending:%d" % [
			int(metrics.get("mesh_build_count", 0)),
			float(metrics.get("mesh_build_average_ms", 0.0)),
			int(metrics.get("queued_chunk_views", 0)),
			int(metrics.get("tracked_chunks", 0)),
			int(metrics.get("async_generation_pending", 0)),
		]

	var frame_text := ""
	if _perf_monitor != null and _perf_monitor.has_method("get_frame_stats"):
		var raw_frame_stats: Variant = _perf_monitor.call("get_frame_stats")
		var frame_stats: Dictionary = raw_frame_stats if raw_frame_stats is Dictionary else {}
		frame_text = "Frame: %.1fms p95:%.1f max:%.1f spike:%d/%d" % [
			float(frame_stats.get("last_ms", 0.0)),
			float(frame_stats.get("p95_ms", 0.0)),
			float(frame_stats.get("max_ms", 0.0)),
			int(frame_stats.get("total_spikes", 0)),
			int(frame_stats.get("total_serious_spikes", 0)),
		]

	# Budget governor state (Phase 5): current state + per-group scales.
	var budget_text := ""
	if _budget_controller != null:
		var stats: Dictionary = _budget_controller.get_governor_stats()
		budget_text = "Budget: %s h:%.2f m:%.2f s:%.2f" % [
			String(stats.get("state", "")),
			float(stats.get("heavy_io_scale", 0.0)),
			float(stats.get("mesh_scale", 0.0)),
			float(stats.get("streaming_scale", 0.0)),
		]

	_label.text = tps_text + "\n" + net_text
	if frame_text != "":
		_label.text += "\n" + frame_text
	if streaming_text != "":
		_label.text += "\n" + streaming_text
	if budget_text != "":
		_label.text += "\n" + budget_text

	# 动态调整背景高度。
	var line_count := _label.get_line_count()
	var line_height := _label.get_line_height()
	var content_height := line_count * line_height + 8
	_bg.offset_bottom = _bg.offset_top + content_height
	_label.offset_bottom = _label.offset_top + content_height


func _format_bytes(bytes_per_sec: float) -> String:
	if bytes_per_sec < 1024.0:
		return "%.0f B" % bytes_per_sec
	elif bytes_per_sec < 1048576.0:
		return "%.1f KB" % (bytes_per_sec / 1024.0)
	else:
		return "%.2f MB" % (bytes_per_sec / 1048576.0)
