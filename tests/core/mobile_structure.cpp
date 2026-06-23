#include <cassert>
#include <iostream>
#include <utility>

#include "core/mobile_structure/dynamic_structure.hpp"
#include "core/world/world_data.hpp"

using namespace science_and_theology;
using namespace science_and_theology::mobile_structure;

namespace {

constexpr const char* kDim = "overworld";
constexpr int kSize = ChunkData::kChunkSize;
constexpr TerrainMaterialId kHull = 9;
constexpr TerrainMaterialId kStone = 1;

ChunkData make_empty_chunk() {
    ChunkData chunk;
    chunk.terrain.resize(kSize, kSize, kSize);
    return chunk;
}

bool is_hull(const TerrainCell& cell) {
    return static_cast<TerrainMaterialId>(cell.material) == kHull;
}

void test_assemble_clears_world_and_registers_entity() {
    WorldData world;
    ChunkData chunk = make_empty_chunk();
    chunk.terrain.set_cell(2, 2, 2, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(3, 2, 2, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(2, 3, 2, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(9, 9, 9, kStone, TF_SOLID | TF_MINEABLE);
    world.set_chunk(kDim, 0, 0, 0, std::move(chunk));

    DynamicStructureRegistry registry;
    AssembleOptions options;
    options.max_blocks = 16;
    options.clear_source_cells = true;

    AssembleResult result = DynamicStructureAssembler::assemble_connected(
        world, registry, kDim, 2, 2, 2, is_hull, options);

    assert(result.success);
    assert(!result.hit_block_limit);
    assert(result.block_count == 3);
    assert(result.terrain_deltas.size() == 3);
    assert(registry.count() == 1);

    const DynamicStructureEntity* entity = registry.get(result.structure_id);
    assert(entity != nullptr);
    assert(entity->snapshot.block_count() == 3);
    assert(entity->mass == 3.0);
    assert(entity->moving);
    assert(entity->transform.position_x == 2.0);
    assert(entity->transform.position_y == 2.0);
    assert(entity->transform.position_z == 2.0);

    const ChunkData* out = world.get_chunk(kDim, 0, 0, 0);
    assert(out != nullptr);
    assert(out->terrain.cell_at(2, 2, 2).material == 0);
    assert(out->terrain.cell_at(3, 2, 2).material == 0);
    assert(out->terrain.cell_at(2, 3, 2).material == 0);
    assert(out->terrain.cell_at(9, 9, 9).material == kStone);
}

void test_transform_snapshot_is_one_entity_payload() {
    DynamicStructureEntity entity;
    entity.id = 42;
    entity.structure_version = 7;
    entity.transform.dimension_id = kDim;
    entity.transform.position_x = 100.0;
    entity.transform.position_y = 200.0;
    entity.transform.position_z = 300.0;
    entity.transform.velocity_x = 10.0;

    DynamicStructureTransformSnapshot snapshot =
        DynamicStructureAssembler::make_transform_snapshot(entity, 1234);

    assert(snapshot.structure_id == 42);
    assert(snapshot.structure_version == 7);
    assert(snapshot.tick == 1234);
    assert(snapshot.transform.position_x == 100.0);
    assert(snapshot.transform.velocity_x == 10.0);
}

void test_disassemble_writes_back_at_new_transform() {
    WorldData world;
    ChunkData chunk = make_empty_chunk();
    chunk.terrain.set_cell(2, 2, 2, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(3, 2, 2, kHull, TF_SOLID | TF_MINEABLE);
    world.set_chunk(kDim, 0, 0, 0, std::move(chunk));

    DynamicStructureRegistry registry;
    AssembleResult assembled = DynamicStructureAssembler::assemble_connected(
        world, registry, kDim, 2, 2, 2, is_hull);
    assert(assembled.success);

    DynamicStructureEntity* entity = registry.get(assembled.structure_id);
    assert(entity != nullptr);
    entity->transform.position_x = 10.0;
    entity->transform.position_y = 2.0;
    entity->transform.position_z = 2.0;

    DisassembleOptions options;
    options.remove_entity = true;
    options.require_air_destination = true;
    DisassembleResult disassembled = DynamicStructureAssembler::disassemble_to_world(
        world, registry, assembled.structure_id, options);

    assert(disassembled.success);
    assert(disassembled.block_count == 2);
    assert(disassembled.terrain_deltas.size() == 2);
    assert(registry.count() == 0);

    const ChunkData* out = world.get_chunk(kDim, 0, 0, 0);
    assert(out != nullptr);
    assert(out->terrain.cell_at(2, 2, 2).material == 0);
    assert(out->terrain.cell_at(3, 2, 2).material == 0);
    assert(out->terrain.cell_at(10, 2, 2).material == kHull);
    assert(out->terrain.cell_at(11, 2, 2).material == kHull);
}

void test_block_limit_fails_without_mutating_world() {
    WorldData world;
    ChunkData chunk = make_empty_chunk();
    chunk.terrain.set_cell(1, 1, 1, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(2, 1, 1, kHull, TF_SOLID | TF_MINEABLE);
    chunk.terrain.set_cell(3, 1, 1, kHull, TF_SOLID | TF_MINEABLE);
    world.set_chunk(kDim, 0, 0, 0, std::move(chunk));

    DynamicStructureRegistry registry;
    AssembleOptions options;
    options.max_blocks = 2;
    options.clear_source_cells = true;

    AssembleResult result = DynamicStructureAssembler::assemble_connected(
        world, registry, kDim, 1, 1, 1, is_hull, options);

    assert(!result.success);
    assert(result.hit_block_limit);
    assert(registry.count() == 0);

    const ChunkData* out = world.get_chunk(kDim, 0, 0, 0);
    assert(out != nullptr);
    assert(out->terrain.cell_at(1, 1, 1).material == kHull);
    assert(out->terrain.cell_at(2, 1, 1).material == kHull);
    assert(out->terrain.cell_at(3, 1, 1).material == kHull);
}

} // namespace

int main() {
    test_assemble_clears_world_and_registers_entity();
    test_transform_snapshot_is_one_entity_payload();
    test_disassemble_writes_back_at_new_transform();
    test_block_limit_fails_without_mutating_world();
    std::cout << "mobile_structure core tests passed\n";
    return 0;
}
