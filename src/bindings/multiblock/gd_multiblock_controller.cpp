#include "gd_multiblock_controller.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "world/gd_world_data.h"
#include "core/multiblock/multiblock_controller.hpp"
#include "core/world/world_data.hpp"

namespace science_and_theology {

GDMultiblockController::GDMultiblockController() = default;
GDMultiblockController::~GDMultiblockController() = default;

godot::Dictionary GDMultiblockController::check_formation(
    GDWorldData* world_data, int64_t entity_id) {

    godot::Dictionary result;
    result["matched"] = false;

    if (world_data == nullptr) {
        result["mismatch_reason"] = "world_data is null";
        return result;
    }

    WorldData* world = world_data->get_world_ptr();
    if (world == nullptr) {
        result["mismatch_reason"] = "world is null";
        return result;
    }

    BlockEntityRegistry& registry = world->block_entity_registry();
    EntityId eid{static_cast<uint64_t>(entity_id)};

    auto check = multiblock::MultiblockControllerBase::check_formation(
        *world, registry, eid);

    result["matched"] = check.matched;

    if (check.matched) {
        // claimed_cells → PackedVector3Array
        godot::PackedVector3Array cells;
        cells.resize(check.claimed_cells.size());
        for (size_t i = 0; i < check.claimed_cells.size(); ++i) {
            const auto& c = check.claimed_cells[i];
            cells[i] = godot::Vector3(
                static_cast<real_t>(c.block_x),
                static_cast<real_t>(c.block_y),
                static_cast<real_t>(c.block_z));
        }
        result["claimed_cells"] = cells;

        // hatch_entities → PackedInt64Array
        godot::PackedInt64Array hatches;
        hatches.resize(check.hatch_entities.size());
        for (size_t i = 0; i < check.hatch_entities.size(); ++i) {
            hatches[i] = static_cast<int64_t>(check.hatch_entities[i].id);
        }
        result["hatch_entities"] = hatches;
    } else {
        result["mismatch_x"] = check.mismatch_x;
        result["mismatch_y"] = check.mismatch_y;
        result["mismatch_z"] = check.mismatch_z;
        result["mismatch_reason"] = godot::String(check.mismatch_reason.c_str());
    }

    return result;
}

void GDMultiblockController::invalidate(
    GDWorldData* world_data, int64_t entity_id) {

    if (world_data == nullptr) return;
    WorldData* world = world_data->get_world_ptr();
    if (world == nullptr) return;

    BlockEntityRegistry& registry = world->block_entity_registry();
    EntityId eid{static_cast<uint64_t>(entity_id)};
    multiblock::MultiblockControllerBase::invalidate(registry, eid);
}

bool GDMultiblockController::has_definition(const godot::String& machine_type) {
    return multiblock::MultiblockControllerBase::has_definition(
        std::string(machine_type.utf8().get_data()));
}

void GDMultiblockController::_bind_methods() {
    using namespace godot;

    ClassDB::bind_method(D_METHOD("check_formation", "world_data", "entity_id"),
                         &GDMultiblockController::check_formation);
    ClassDB::bind_method(D_METHOD("invalidate", "world_data", "entity_id"),
                         &GDMultiblockController::invalidate);
    ClassDB::bind_static_method("GDMultiblockController",
        D_METHOD("has_definition", "machine_type"),
        &GDMultiblockController::has_definition);
}

} // namespace science_and_theology
