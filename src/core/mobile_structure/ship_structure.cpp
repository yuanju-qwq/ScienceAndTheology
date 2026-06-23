#include "ship_structure.hpp"

#include <algorithm>

#include "../world/world_data.hpp"

namespace science_and_theology::mobile_structure {

bool ShipStructureService::material_allowed(
    TerrainMaterialId material,
    const std::vector<TerrainMaterialId>& allowed_materials) {
    if (allowed_materials.empty()) {
        return material != 0;
    }
    return std::find(allowed_materials.begin(), allowed_materials.end(), material)
        != allowed_materials.end();
}

ShipAssembleResult ShipStructureService::assemble_ship_from_world(
    WorldData& world,
    const std::string& dimension_id,
    int32_t seed_x, int32_t seed_y, int32_t seed_z,
    const ShipAssembleOptions& options) {
    AssembleOptions assemble_options;
    assemble_options.max_blocks = options.max_blocks;
    assemble_options.clear_source_cells = options.clear_source_cells;
    assemble_options.fail_on_missing_chunks = options.fail_on_missing_chunks;

    auto include = [&options](const TerrainCell& cell) {
        return material_allowed(
            static_cast<TerrainMaterialId>(cell.material),
            options.allowed_materials);
    };

    ShipAssembleResult result;
    result.base = DynamicStructureAssembler::assemble_connected(
        world,
        world.mobile_structure_registry(),
        dimension_id,
        seed_x, seed_y, seed_z,
        include,
        assemble_options);

    if (result.base.success) {
        const DynamicStructureEntity* entity =
            world.mobile_structure_registry().get(result.base.structure_id);
        if (entity != nullptr) {
            result.transform_snapshot =
                DynamicStructureAssembler::make_transform_snapshot(
                    *entity, world.current_tick());
        }
    }

    return result;
}

DisassembleResult ShipStructureService::disassemble_ship_to_world(
    WorldData& world,
    DynamicStructureId ship_id,
    const DisassembleOptions& options) {
    return DynamicStructureAssembler::disassemble_to_world(
        world,
        world.mobile_structure_registry(),
        ship_id,
        options);
}

DynamicStructureTransformSnapshot ShipStructureService::make_transform_snapshot(
    const WorldData& world,
    DynamicStructureId ship_id,
    int64_t tick) {
    DynamicStructureTransformSnapshot snapshot;
    const DynamicStructureEntity* entity =
        world.mobile_structure_registry().get(ship_id);
    if (entity == nullptr) return snapshot;
    return DynamicStructureAssembler::make_transform_snapshot(*entity, tick);
}

} // namespace science_and_theology::mobile_structure
