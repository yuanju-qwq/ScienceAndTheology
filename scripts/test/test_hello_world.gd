extends Node

func _ready() -> void:
	_run_tests()


func _run_tests() -> void:
	print("=== GDExtension Hello World Test Suite ===")

	_test_basic_hello()
	_test_property()
	_test_signal()
	_test_node_lifecycle()

	print("=== All tests passed! ===")


func _test_basic_hello() -> void:
	print("--- Test: basic hello method ---")

	var hw := GDHelloWorld.new()
	var result := hw.hello("World")

	assert(result.begins_with("Hello"), "Expected 'Hello' prefix, got: " + result)
	print("  Result: ", result)


func _test_property() -> void:
	print("--- Test: greeting_prefix property ---")

	var hw := GDHelloWorld.new()
	assert(hw.greeting_prefix == "Hello", "Default prefix should be 'Hello'")

	hw.greeting_prefix = "你好"
	assert(hw.greeting_prefix == "你好", "Prefix should be updated to '你好'")

	var result := hw.hello("世界")
	assert(result.begins_with("你好"), "Expected '你好' prefix, got: " + result)
	print("  Result: ", result)


func _test_signal() -> void:
	print("--- Test: greeting_sent signal ---")

	var hw := GDHelloWorld.new()
	var received_message := ""

	hw.greeting_sent.connect(func(msg: String):
		received_message = msg
		print("  Signal received: ", msg)
	)

	hw.send_greeting("Tester")
	assert(not received_message.is_empty(), "Signal should have been emitted")
	print("  Signal test passed")


func _test_node_lifecycle() -> void:
	print("--- Test: node lifecycle ---")

	var hw := GDHelloWorld.new()
	add_child(hw)

	await get_tree().process_frame

	assert(hw.is_inside_tree(), "Node should be in scene tree")
	print("  GDHelloWorld is in tree: ", hw.is_inside_tree())

	remove_child(hw)
	hw.queue_free()