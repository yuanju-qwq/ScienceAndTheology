extends Control
class_name LoadingOverlay

# Full-screen loading overlay shown while the spawn planet's initial chunks
# are being generated and their collision bodies built.
# Created and managed by PlayerController; fades out when _spawn_freeze releases.

var _world: Node  # ChunkRendererBridge
var _initial_chunks: Array[Vector3i] = []
var _progress_bar: ProgressBar
var _status_label: Label
var _fading := false


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP  # Block input during loading

	var bg := ColorRect.new()
	bg.color = Color(0.02, 0.025, 0.04, 0.98)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(bg)

	var vbox := VBoxContainer.new()
	vbox.alignment = BoxContainer.ALIGNMENT_CENTER
	vbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	vbox.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(vbox)

	var title := Label.new()
	title.text = "Loading World"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 28)
	title.add_theme_color_override("font_color", Color(0.85, 0.87, 0.90, 1.0))
	vbox.add_child(title)

	_progress_bar = ProgressBar.new()
	_progress_bar.min_value = 0.0
	_progress_bar.max_value = 100.0
	_progress_bar.value = 0.0
	_progress_bar.custom_minimum_size = Vector2(400, 24)
	vbox.add_child(_progress_bar)

	_status_label = Label.new()
	_status_label.text = "Generating terrain..."
	_status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_status_label.add_theme_color_override("font_color", Color(0.50, 0.52, 0.56, 1.0))
	vbox.add_child(_status_label)


func setup(world: Node, initial_chunks: Array[Vector3i]) -> void:
	_world = world
	_initial_chunks = initial_chunks


func _process(_delta: float) -> void:
	if _fading or _world == null or _initial_chunks.is_empty():
		return
	var visible_count := 0
	for chunk: Vector3i in _initial_chunks:
		if _world.is_chunk_visible(chunk):
			visible_count += 1
	var ratio := float(visible_count) / float(_initial_chunks.size())
	_progress_bar.value = ratio * 100.0
	_status_label.text = "Generating terrain... %d/%d chunks" % [visible_count, _initial_chunks.size()]


func fade_out_and_free() -> void:
	if _fading:
		return
	_fading = true
	_status_label.text = "Entering world..."
	_progress_bar.value = 100.0
	mouse_filter = Control.MOUSE_FILTER_IGNORE  # Let input through during fade
	var tween := create_tween()
	tween.tween_property(self, "modulate:a", 0.0, 0.5)
	tween.tween_callback(queue_free)
