#include "gd_ship_command_bridge.h"

#include <string>
#include <vector>

#include <godot_cpp/core/class_db.hpp>

#include "bindings/world/gd_world_data.h"
#include "core/mobile_structure/ship_structure.hpp"
#include "core/world/chunk_data.hpp"
#include "core/world/world_data.hpp"

namespace science_and_theology {

using namespace godot;
using namespace mobile_structure;

namespace {

Vector3i chunk_from_delta(const MobileTerrainDelta& delta) {
    return Vector3i(delta.chunk_x, delta.chunk_y, delta.chunk_z);
}

Vector3i local_from_delta(const MobileTerrainDelta& delta) {
    return Vector3i(delta.local_x, delta.local_y, delta.local_z);
}

Dictionary transform_to_dict(const DynamicStructureTransform& transform) {
    Dictionary dict;
    dict["dimension"] = String(transform.dimension_id.c_str());
    dict["position"] = Vector3(
        static_cast<float>(transform.position_x),
        static_cast<float>(transform.position_y),
        static_cast<float>(transform.position_z));
    dict["rotation"] = Vector3(
        transform.rotation_x,
        transform.rotation_y,
        transform.rotation_z);
    dict["rotation_w"] = transform.rotation_w;
    dict["velocity"] = Vector3(
        static_cast<float>(transform.velocity_x),
        static_cast<float>(transform.velocity_y),
        static_cast<float>(transform.velocity_z));
    dict["angular_velocity"] = Vector3(
        static_cast<float>(transform.angular_velocity_x),
        static_cast<float>(transform.angular_velocity_y),
        static_cast<float>(transform.angular_velocity_z));
    return dict;
}

} // namespace

void GDShipCommandBridge::set_world_data(Resource* world) {
    world_data_ = Object::cast_to<GDWorldData>(world);
}

Resource* GDShipCommandBridge::get_world_data() const {
    return world_data_;
}

Dictionary GDShipCommandBridge::assemble_ship(
    const StringName& dimension,
    const Vector3i& seed_cell,
    const Array& allowed_material_ids,
    int64_t max_blocks) {
    if (world_data_ == nullptr || world_data_->get_world_ptr() == nullptr) {
        return reject("assemble_ship", "world data is not available");
    }
    if (max_blocks <= 0) {
        return reject("assemble_ship", "max_blocks must be greater than zero");
    }

    ShipAssembleOptions options;
    options.max_blocks = static_cast<size_t>(max_blocks);
    options.clear_source_cells = true;
    options.fail_on_missing_chunks = false;
    options.allowed_materials.reserve(static_cast<size_t>(allowed_material_ids.size()));
    for (int64_t i = 0; i < allowed_material_ids.size(); ++i) {
        options.allowed_materials.push_back(
            static_cast<TerrainMaterialId>(static_cast<int64_t>(allowed_material_ids[i])));
    }

    WorldData* world = world_data_->get_world_ptr();
    const std::string dim(String(dimension).utf8().get_data());
    ShipAssembleResult result = ShipStructureService::assemble_ship_from_world(
        *world, dim, seed_cell.x, seed_cell.y, seed_cell.z, options);

    if (!result.base.success) {
        return reject("assemble_ship", String(result.base.error.c_str()));
    }

    for (const MobileTerrainDelta& delta : result.base.terrain_deltas) {
        emit_terrain_delta(delta);
    }

    const DynamicStructureEntity* entity =
        world->mobile_structure_registry().get(result.base.structure_id);
    if (entity == nullptr) {
        return reject("assemble_ship", "assembled structure disappeared");
    }

    Dictionary structure = structure_entity_to_dict(*entity);
    emit_signal("dynamic_structure_assembled", structure);
    emit_signal("dynamic_structure_transform_synced",
                transform_snapshot_to_dict(result.transform_snapshot));

    Dictionary out;
    out["ok"] = true;
    out["type"] = "assemble_ship";
    out["structure"] = structure;
    out["block_count"] = static_cast<int64_t>(result.base.block_count);
    return out;
}

Dictionary GDShipCommandBridge::disassemble_ship(int64_t structure_id) {
    if (world_data_ == nullptr || world_data_->get_world_ptr() == nullptr) {
        return reject("disassemble_ship", "world data is not available");
    }
    if (structure_id <= 0) {
        return reject("disassemble_ship", "structure_id is invalid");
    }

    WorldData* world = world_data_->get_world_ptr();
    DisassembleOptions options;
    options.remove_entity = true;
    options.require_air_destination = true;

    DisassembleResult result = ShipStructureService::disassemble_ship_to_world(
        *world,
        static_cast<DynamicStructureId>(structure_id),
        options);

    if (!result.success) {
        return reject("disassemble_ship", String(result.error.c_str()));
    }

    for (const MobileTerrainDelta& delta : result.terrain_deltas) {
        emit_terrain_delta(delta);
    }
    emit_signal("dynamic_structure_removed", structure_id);

    Dictionary out;
    out["ok"] = true;
    out["type"] = "disassemble_ship";
    out["structure_id"] = structure_id;
    out["block_count"] = static_cast<int64_t>(result.block_count);
    return out;
}

Dictionary GDShipCommandBridge::set_ship_transform(
    int64_t structure_id,
    const Vector3& position,
    const Vector3& velocity) {
    if (world_data_ == nullptr || world_data_->get_world_ptr() == nullptr) {
        return reject("set_ship_transform", "world data is not available");
    }
    if (structure_id <= 0) {
        return reject("set_ship_transform", "structure_id is invalid");
    }

    WorldData* world = world_data_->get_world_ptr();
    DynamicStructureEntity* entity =
        world->mobile_structure_registry().get(static_cast<DynamicStructureId>(structure_id));
    if (entity == nullptr) {
        return reject("set_ship_transform", "dynamic structure not found");
    }

    entity->transform.position_x = position.x;
    entity->transform.position_y = position.y;
    entity->transform.position_z = position.z;
    entity->transform.velocity_x = velocity.x;
    entity->transform.velocity_y = velocity.y;
    entity->transform.velocity_z = velocity.z;

    DynamicStructureTransformSnapshot snapshot =
        DynamicStructureAssembler::make_transform_snapshot(
            *entity, world->current_tick());
    Dictionary snapshot_dict = transform_snapshot_to_dict(snapshot);
    emit_signal("dynamic_structure_transform_synced", snapshot_dict);

    Dictionary out;
    out["ok"] = true;
    out["type"] = "set_ship_transform";
    out["snapshot"] = snapshot_dict;
    return out;
}

Dictionary GDShipCommandBridge::reject(
    const StringName& command_type,
    const String& reason) {
    emit_signal("command_rejected", command_type, reason);
    Dictionary out;
    out["ok"] = false;
    out["type"] = command_type;
    out["reason"] = reason;
    return out;
}

Dictionary GDShipCommandBridge::transform_snapshot_to_dict(
    const DynamicStructureTransformSnapshot& snapshot) {
    Dictionary dict;
    dict["structure_id"] = static_cast<int64_t>(snapshot.structure_id);
    dict["structure_version"] = static_cast<int64_t>(snapshot.structure_version);
    dict["tick"] = static_cast<int64_t>(snapshot.tick);
    dict["transform"] = transform_to_dict(snapshot.transform);
    return dict;
}

Dictionary GDShipCommandBridge::structure_entity_to_dict(
    const DynamicStructureEntity& entity) {
    Dictionary dict;
    dict["structure_id"] = static_cast<int64_t>(entity.id);
    dict["structure_version"] = static_cast<int64_t>(entity.structure_version);
    dict["block_count"] = static_cast<int64_t>(entity.snapshot.block_count());
    dict["mass"] = entity.mass;
    dict["moving"] = entity.moving;
    dict["dirty_mesh"] = entity.dirty_mesh;
    dict["transform"] = transform_to_dict(entity.transform);

    Dictionary bounds;
    bounds["min"] = Vector3i(entity.snapshot.min_x, entity.snapshot.min_y, entity.snapshot.min_z);
    bounds["max"] = Vector3i(entity.snapshot.max_x, entity.snapshot.max_y, entity.snapshot.max_z);
    dict["bounds"] = bounds;

    Array blocks;
    blocks.resize(static_cast<int64_t>(entity.snapshot.blocks.size()));
    for (int64_t i = 0; i < static_cast<int64_t>(entity.snapshot.blocks.size()); ++i) {
        const LocalStructureBlock& block = entity.snapshot.blocks[static_cast<size_t>(i)];
        Dictionary block_dict;
        block_dict["local"] = Vector3i(block.local_x, block.local_y, block.local_z);
        block_dict["material"] = static_cast<int64_t>(block.material);
        block_dict["flags"] = static_cast<int64_t>(block.flags);
        blocks[i] = block_dict;
    }
    dict["blocks"] = blocks;
    return dict;
}

void GDShipCommandBridge::emit_terrain_delta(const MobileTerrainDelta& delta) {
    emit_signal("terrain_cell_synced",
                StringName(delta.dimension_id.c_str()),
                chunk_from_delta(delta),
                local_from_delta(delta),
                static_cast<int64_t>(delta.old_material),
                static_cast<int64_t>(delta.new_material));
}

void GDShipCommandBridge::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_world_data", "world"),
                         &GDShipCommandBridge::set_world_data);
    ClassDB::bind_method(D_METHOD("get_world_data"),
                         &GDShipCommandBridge::get_world_data);
    ClassDB::bind_method(D_METHOD("assemble_ship", "dimension", "seed_cell", "allowed_material_ids", "max_blocks"),
                         &GDShipCommandBridge::assemble_ship);
    ClassDB::bind_method(D_METHOD("disassemble_ship", "structure_id"),
                         &GDShipCommandBridge::disassemble_ship);
    ClassDB::bind_method(D_METHOD("set_ship_transform", "structure_id", "position", "velocity"),
                         &GDShipCommandBridge::set_ship_transform);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "world_data",
                              PROPERTY_HINT_RESOURCE_TYPE, "GDWorldData"),
                 "set_world_data", "get_world_data");

    ADD_SIGNAL(MethodInfo("terrain_cell_synced",
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "chunk"),
        PropertyInfo(Variant::VECTOR3I, "local"),
        PropertyInfo(Variant::INT, "old_material"),
        PropertyInfo(Variant::INT, "new_material")));
    ADD_SIGNAL(MethodInfo("dynamic_structure_assembled",
        PropertyInfo(Variant::DICTIONARY, "structure")));
    ADD_SIGNAL(MethodInfo("dynamic_structure_transform_synced",
        PropertyInfo(Variant::DICTIONARY, "snapshot")));
    ADD_SIGNAL(MethodInfo("dynamic_structure_removed",
        PropertyInfo(Variant::INT, "structure_id")));
    ADD_SIGNAL(MethodInfo("command_rejected",
        PropertyInfo(Variant::STRING_NAME, "command_type"),
        PropertyInfo(Variant::STRING, "reason")));
}

} // namespace science_and_theology
