#pragma once

#include <godot_cpp/classes/node.hpp>

namespace science_and_theology {

/// A minimal GDExtension example class.
/// Demonstrates: methods, properties, signals, and GDScript interop.
class GDHelloWorld : public godot::Node {
    GDCLASS(GDHelloWorld, godot::Node)

    godot::String greeting_prefix;

protected:
    static void _bind_methods();

public:
    GDHelloWorld();
    ~GDHelloWorld() override;

    // Godot lifecycle
    void _ready() override;
    void _process(double delta) override;

    // Methods exposed to GDScript
    godot::String hello(const godot::String &name) const;
    void send_greeting(const godot::String &name);

    // Property getter/setter
    godot::String get_greeting_prefix() const;
    void set_greeting_prefix(const godot::String &p_prefix);

    // Signals
    // greeting_sent(String message)
};

} // namespace science_and_theology