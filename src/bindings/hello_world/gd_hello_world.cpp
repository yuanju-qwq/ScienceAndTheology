#include "gd_hello_world.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace science_and_theology {
using namespace godot;

GDHelloWorld::GDHelloWorld()
    : greeting_prefix("Hello") {
}

GDHelloWorld::~GDHelloWorld() = default;

void GDHelloWorld::_bind_methods() {
    // Method bindings
    ClassDB::bind_method(D_METHOD("hello", "name"), &GDHelloWorld::hello);
    ClassDB::bind_method(D_METHOD("send_greeting", "name"), &GDHelloWorld::send_greeting);

    // Property binding
    ClassDB::bind_method(D_METHOD("get_greeting_prefix"), &GDHelloWorld::get_greeting_prefix);
    ClassDB::bind_method(D_METHOD("set_greeting_prefix", "prefix"), &GDHelloWorld::set_greeting_prefix);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "greeting_prefix"),
                 "set_greeting_prefix", "get_greeting_prefix");

    // Signal binding
    ADD_SIGNAL(MethodInfo("greeting_sent", PropertyInfo(Variant::STRING, "message")));
}

void GDHelloWorld::_ready() {
    // Node is ready in the scene tree
}

void GDHelloWorld::_process(double delta) {
    // Called every frame; use for time-based logic
}

godot::String GDHelloWorld::hello(const godot::String &name) const {
    godot::String message = greeting_prefix + godot::String(", ") + name + godot::String("! GDExtension is working.");
    UtilityFunctions::print(message);
    return message;
}

void GDHelloWorld::send_greeting(const godot::String &name) {
    godot::String message = greeting_prefix + ", " + name + "! (via signal)";
    emit_signal("greeting_sent", message);
}

godot::String GDHelloWorld::get_greeting_prefix() const {
    return greeting_prefix;
}

void GDHelloWorld::set_greeting_prefix(const godot::String &p_prefix) {
    greeting_prefix = p_prefix;
}

} // namespace science_and_theology