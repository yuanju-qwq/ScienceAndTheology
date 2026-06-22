# KeyBindings — 管理可自定义的按键绑定，使用 Godot InputMap 系统。
# 默认绑定在代码中定义，用户修改后持久化到 user://keybindings.cfg。
# 只管理"动作类"按键（背包、合成台等），移动键（WASD）保持硬编码。
extends Node

const SAVE_PATH := "user://keybindings.cfg"
const SECTION := "bindings"

# 动作定义：[action_name, display_name, default_keycode]
# 只包含可在设置界面修改的动作。
const ACTION_DEFS: Array[Array] = [
	[&"toggle_inventory",  "背包",       KEY_E],
	[&"toggle_crafting",   "合成台",     KEY_C],
	[&"toggle_wiki",       "百科",       KEY_B],
	[&"toggle_quest_book", "任务书",     KEY_J],
	[&"toggle_mouse",      "释放鼠标",   KEY_TAB],
	[&"toggle_debug",      "调试面板",   KEY_F3],
	[&"toggle_build_mode", "建造坐标",   KEY_G],
	[&"toggle_nei_panel",  "NEI物品查询", KEY_R],
]


func _ready() -> void:
	_ensure_actions()
	_load()


# 确保所有动作在 InputMap 中注册。
func _ensure_actions() -> void:
	for def in ACTION_DEFS:
		var action: StringName = def[0]
		if not InputMap.has_action(action):
			InputMap.add_action(action)


# 加载用户自定义绑定，覆盖默认值。
func _load() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(SAVE_PATH) != OK:
		# 无配置文件，使用默认绑定。
		_apply_defaults()
		return
	for def in ACTION_DEFS:
		var action: StringName = def[0]
		var default_key: Key = def[2]
		var key: Key = cfg.get_value(SECTION, String(action), default_key) as Key
		_set_action_key(action, key)


# 保存当前绑定到配置文件。
func save() -> void:
	var cfg := ConfigFile.new()
	for def in ACTION_DEFS:
		var action: StringName = def[0]
		var events := InputMap.action_get_events(action)
		if events.size() > 0 and events[0] is InputEventKey:
			var key: Key = (events[0] as InputEventKey).keycode
			cfg.set_value(SECTION, String(action), key)
		else:
			cfg.set_value(SECTION, String(action), def[2])
	cfg.save(SAVE_PATH)


# 应用默认绑定。
func _apply_defaults() -> void:
	for def in ACTION_DEFS:
		var action: StringName = def[0]
		var default_key: Key = def[2]
		_set_action_key(action, default_key)


# 设置某个动作的按键（替换所有事件）。
func _set_action_key(action: StringName, key: Key) -> void:
	InputMap.action_erase_events(action)
	var event := InputEventKey.new()
	event.keycode = key
	InputMap.action_add_event(action, event)


# 获取某个动作当前绑定的按键码。
func get_action_key(action: StringName) -> Key:
	var events := InputMap.action_get_events(action)
	if events.size() > 0 and events[0] is InputEventKey:
		return (events[0] as InputEventKey).keycode
	# 回退到默认值。
	for def in ACTION_DEFS:
		if def[0] == action:
			return def[2] as Key
	return KEY_UNKNOWN


# 重新绑定某个动作，并保存。
func rebind(action: StringName, key: Key) -> void:
	_set_action_key(action, key)
	save()


# 重置所有绑定到默认值，并保存。
func reset_all() -> void:
	_apply_defaults()
	save()


# 获取按键的可读名称。
static func key_name(key: Key) -> String:
	if key == KEY_UNKNOWN:
		return "—"
	# 特殊键名映射。
	match key:
		KEY_SPACE:     return "Space"
		KEY_TAB:       return "Tab"
		KEY_ESCAPE:    return "Esc"
		KEY_ENTER:     return "Enter"
		KEY_SHIFT:     return "Shift"
		KEY_CTRL:      return "Ctrl"
		KEY_ALT:       return "Alt"
		KEY_BACKSPACE: return "Backspace"
		KEY_INSERT:    return "Insert"
		KEY_DELETE:    return "Delete"
		KEY_HOME:      return "Home"
		KEY_END:       return "End"
		KEY_PAGEUP:    return "PageUp"
		KEY_PAGEDOWN:  return "PageDown"
		KEY_UP:        return "↑"
		KEY_DOWN:      return "↓"
		KEY_LEFT:      return "←"
		KEY_RIGHT:     return "→"
		KEY_F1:        return "F1"
		KEY_F2:        return "F2"
		KEY_F3:        return "F3"
		KEY_F4:        return "F4"
		KEY_F5:        return "F5"
		KEY_F6:        return "F6"
		KEY_F7:        return "F7"
		KEY_F8:        return "F8"
		KEY_F9:        return "F9"
		KEY_F10:       return "F10"
		KEY_F11:       return "F11"
		KEY_F12:       return "F12"
	var name := OS.get_keycode_string(key)
	return name if name != "" else "Key_%d" % key
