#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace science_and_theology {

class GDAnvilManager : public godot::Node {
    GDCLASS(GDAnvilManager, godot::Node)

public:
    void _ready() override;

    bool place_anvil(const godot::StringName& dim, const godot::Vector3i& cell);
    bool remove_anvil(const godot::StringName& dim, const godot::Vector3i& cell);
    bool has_anvil(const godot::StringName& dim, const godot::Vector3i& cell) const;

    // Returns all anvil cells as an Array of {dimension: String, cell: Vector3i}.
    // Used by MachineCollisionBridge to re-apply the overlay on load/dimension switch.
    godot::Array get_all_anvils() const;

    // Weld: returns a Dictionary with {ok, ingot_item_id, count} on success.
    // The caller (GDScript) must handle item consumption.
    godot::Dictionary weld(const godot::StringName& dim, const godot::Vector3i& cell);

    void clear();

protected:
    static void _bind_methods();

private:
    struct AnvilKey {
        std::string d; int32_t x = 0; int32_t y = 0; int32_t z = 0;
        bool operator==(const AnvilKey& o) const {
            return d == o.d && x == o.x && y == o.y && z == o.z;
        }
    };
    struct AnvilKeyHash { size_t operator()(const AnvilKey& k) const; };
    static AnvilKey mk(const godot::StringName& d, const godot::Vector3i& c);

    std::unordered_map<AnvilKey, bool, AnvilKeyHash> anvils_;
};

} // namespace science_and_theology
