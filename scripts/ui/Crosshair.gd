# Crosshair — 屏幕中心十字准星，使用 _draw() 直接绘制。
extends Control


func _ready() -> void:
	set_anchors_preset(PRESET_CENTER)
	# 让 Control 覆盖整个视口，这样 _draw() 的坐标以屏幕中心为原点。
	anchor_left = 0.0
	anchor_top = 0.0
	anchor_right = 1.0
	anchor_bottom = 1.0
	offset_left = 0.0
	offset_top = 0.0
	offset_right = 0.0
	offset_bottom = 0.0
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func _draw() -> void:
	var center := size * 0.5
	var half_len := 12.0
	var gap := 4.0
	var width := 2.0
	var color := Color(1.0, 1.0, 1.0, 0.85)

	# 横线（左右两段，中间留空隙）
	draw_rect(Rect2(center.x - half_len, center.y - width * 0.5, half_len - gap, width), color)
	draw_rect(Rect2(center.x + gap, center.y - width * 0.5, half_len - gap, width), color)
	# 竖线（上下两段，中间留空隙）
	draw_rect(Rect2(center.x - width * 0.5, center.y - half_len, width, half_len - gap), color)
	draw_rect(Rect2(center.x - width * 0.5, center.y + gap, width, half_len - gap), color)
