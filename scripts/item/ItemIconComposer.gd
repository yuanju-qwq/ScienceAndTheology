class_name ItemIconComposer
extends RefCounted

const DEFAULT_SIZE := 32

static var _cache: Dictionary = {}
static var _warned_missing: Dictionary = {}


static func clear_cache() -> void:
	_cache.clear()


static func compose_tinted_icon(
		base_path: String,
		tint: Color,
		overlay_path: String = "",
		target_size: int = DEFAULT_SIZE) -> Texture2D:
	var key := "%s|%s|%s|%d" % [base_path, tint.to_html(true), overlay_path, target_size]
	if _cache.has(key):
		return _cache[key]

	var base_img := _load_image(base_path, target_size)
	if base_img == null:
		return null

	var out := Image.create(target_size, target_size, false, Image.FORMAT_RGBA8)
	for y in range(target_size):
		for x in range(target_size):
			var src := base_img.get_pixel(x, y)
			if src.a <= 0.0:
				out.set_pixel(x, y, Color(0, 0, 0, 0))
				continue
			var shade := (src.r * 0.299) + (src.g * 0.587) + (src.b * 0.114)
			out.set_pixel(x, y, Color(tint.r * shade, tint.g * shade, tint.b * shade, src.a))

	if not overlay_path.is_empty():
		var overlay_img := _load_image(overlay_path, target_size)
		if overlay_img != null:
			_blend_over(out, overlay_img)

	var texture := ImageTexture.create_from_image(out)
	_cache[key] = texture
	return texture


static func _load_image(path: String, target_size: int) -> Image:
	if path.is_empty():
		return null
	if not ResourceLoader.exists(path):
		_warn_missing(path)
		return null
	var tex := load(path) as Texture2D
	if tex == null:
		_warn_missing(path)
		return null
	var img := tex.get_image()
	if img == null:
		_warn_missing(path)
		return null
	img = img.duplicate()
	img.convert(Image.FORMAT_RGBA8)
	if img.get_width() != target_size or img.get_height() != target_size:
		img.resize(target_size, target_size, Image.INTERPOLATE_NEAREST)
	return img


static func _warn_missing(path: String) -> void:
	if _warned_missing.has(path):
		return
	_warned_missing[path] = true
	push_warning("ItemIconComposer: missing tint icon resource '%s'" % path)


static func _blend_over(dst: Image, src: Image) -> void:
	var w: int = min(dst.get_width(), src.get_width())
	var h: int = min(dst.get_height(), src.get_height())
	for y in range(h):
		for x in range(w):
			var over: Color = src.get_pixel(x, y)
			if over.a <= 0.0:
				continue
			var under: Color = dst.get_pixel(x, y)
			var out_a: float = over.a + under.a * (1.0 - over.a)
			if out_a <= 0.0:
				dst.set_pixel(x, y, Color(0, 0, 0, 0))
				continue
			var out_r: float = (over.r * over.a + under.r * under.a * (1.0 - over.a)) / out_a
			var out_g: float = (over.g * over.a + under.g * under.a * (1.0 - over.a)) / out_a
			var out_b: float = (over.b * over.a + under.b * under.a * (1.0 - over.a)) / out_a
			dst.set_pixel(x, y, Color(out_r, out_g, out_b, out_a))
