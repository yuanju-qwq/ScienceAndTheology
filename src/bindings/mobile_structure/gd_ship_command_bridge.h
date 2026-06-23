#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include "core/mobile_structure/dynamic_structure.hpp"

namespace science_and_theology {

class GDWorldData;

class GDShipCommandBridge : public godot::Node {
    GDCLASS(GDShipCommandBridge, godot::Node)

public:
    GDShipCommandBridge() = default;
    ~GDShipCommandBridge() override = default;

    void set_world_data(godot::Resource* world);
    godot::Resource* get_world_data() const;

    godot::Dictionary assemble_ship(
        const godot::StringName& dimension,
        const godot::Vector3i& seed_cell,
        const godot::Array& allowed_material_ids,
        int64_t max_blocks);

    godot::Dictionary disassemble_ship(int64_t structure_id);

    godot::Dictionary set_ship_transform(
        int64_t structure_id,
        const godot::Vector3& position,
        const godot::Vector3& velocity);

protected:
    static void _bind_methods();

private:
    godot::Dictionary reject(const godot::StringName& command_type,
                             const godot::String& reason);

    static godot::Dictionary transform_snapshot_to_dict(
        const mobile_structure::DynamicStructureTransformSnapshot& snapshot);
    static godot::Dictionary structure_entity_to_dict(
        const mobile_structure::DynamicStructureEntity& entity);

    void emit_terrain_delta(const mobile_structure::MobileTerrainDelta& delta);

    GDWorldData* world_data_ = nullptr;
};

} // namespace science_and_theology
