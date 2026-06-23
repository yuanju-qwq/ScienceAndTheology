class_name WikiUI
extends Control

## Deprecated compatibility shell.
## The old encyclopedia content has been replaced by NeiPanel + NEIIndex.
## WorldMap still contains a WikiUI node until the scene is edited safely,
## so this stub prevents stale scene references from breaking startup.

var _is_open := false

func _ready() -> void:
	visible = false
	mouse_filter = Control.MOUSE_FILTER_IGNORE

func toggle() -> void:
	_is_open = not _is_open
	visible = false
