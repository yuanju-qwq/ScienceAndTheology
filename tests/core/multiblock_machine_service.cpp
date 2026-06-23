#include <cassert>
#include <iostream>
#include <memory>
#include <utility>

#include "core/multiblock/multiblock_controller.hpp"
#include "core/multiblock/multiblock_machine_service.hpp"
#include "core/multiblock/structure_definition.hpp"
#include "core/world/world_data.hpp"

using namespace science_and_theology;
using namespace science_and_theology::multiblock;

namespace {

constexpr const char* kDim = "overworld";
constexpr int kSize = ChunkData::kChunkSize;

ChunkData make_empty_chunk() {
    ChunkData chunk;
    chunk.terrain.resize(kSize, kSize, kSize);
    return chunk;
}

std::shared_ptr<StructureDefinition> build_input_hatch_definition() {
    return DeclarativePatternBuilder::start()
        .aisle({"~H"})
        .where('H', Elements::hatch(HATCH_ITEM_INPUT))
        .build_structure_definition("svc_input_machine");
}

void test_controller_forms_with_matching_hatch() {
    MultiblockControllerBase::clear_registry();
    MultiblockControllerBase::register_definition(
        "svc_input_machine", build_input_hatch_definition);

    WorldData world;
    world.set_chunk(kDim, 0, 0, 0, make_empty_chunk());

    auto& registry = world.block_entity_registry();
    EntityId controller = registry.register_machine_entity(
        kDim, 5, 5, 5, "svc_input_machine", 4);
    EntityId hatch = registry.register_machine_entity(
        kDim, 6, 5, 5, "item_input_bus", 4);

    MachineFormationUpdate update =
        MultiblockMachineService::check_controller(world, controller);

    assert(update.checked);
    assert(update.formed);
    assert(update.error.empty());
    assert(update.hatches.item_inputs == 1);
    assert(update.hatches.mask & HATCH_ITEM_INPUT);

    const MachineBlockEntityState* state = registry.get_machine_state(controller);
    assert(state != nullptr);
    assert(state->formed);
    assert(state->hatch_entities.size() == 1);
    assert(state->hatch_entities[0] == hatch);
    assert(MultiblockMachineService::can_tick_machine(world, controller));
}

void test_controller_rejects_wrong_hatch_type() {
    MultiblockControllerBase::clear_registry();
    MultiblockControllerBase::register_definition(
        "svc_input_machine", build_input_hatch_definition);

    WorldData world;
    world.set_chunk(kDim, 0, 0, 0, make_empty_chunk());

    auto& registry = world.block_entity_registry();
    EntityId controller = registry.register_machine_entity(
        kDim, 5, 5, 5, "svc_input_machine", 4);
    registry.register_machine_entity(
        kDim, 6, 5, 5, "item_output_bus", 4);

    MachineFormationUpdate update =
        MultiblockMachineService::check_controller(world, controller);

    assert(update.checked);
    assert(!update.formed);
    assert(!update.error.empty());
    assert(!MultiblockMachineService::can_tick_machine(world, controller));
}

void test_trivial_machine_forms_and_ticks() {
    MultiblockControllerBase::clear_registry();

    WorldData world;
    world.set_chunk(kDim, 0, 0, 0, make_empty_chunk());

    auto& registry = world.block_entity_registry();
    EntityId simple = registry.register_machine_entity(
        kDim, 1, 1, 1, "simple_furnace", 4);

    MachineFormationUpdate update =
        MultiblockMachineService::check_controller(world, simple);

    assert(update.checked);
    assert(update.formed);
    assert(MultiblockMachineService::can_tick_machine(world, simple));
}

void test_cell_changed_rechecks_nearby_controller() {
    MultiblockControllerBase::clear_registry();
    MultiblockControllerBase::register_definition(
        "svc_input_machine", build_input_hatch_definition);

    WorldData world;
    world.set_chunk(kDim, 0, 0, 0, make_empty_chunk());

    auto& registry = world.block_entity_registry();
    EntityId controller = registry.register_machine_entity(
        kDim, 10, 5, 5, "svc_input_machine", 4);
    registry.register_machine_entity(kDim, 11, 5, 5, "item_input_bus", 4);

    auto updates = MultiblockMachineService::on_cell_changed(
        world, kDim, 11, 5, 5);

    bool found_controller = false;
    for (const auto& update : updates) {
        if (update.machine_id == controller && update.formed) {
            found_controller = true;
        }
    }
    assert(found_controller);
    assert(registry.get_machine_state(controller)->formed);
}

} // namespace

int main() {
    test_controller_forms_with_matching_hatch();
    test_controller_rejects_wrong_hatch_type();
    test_trivial_machine_forms_and_ticks();
    test_cell_changed_rechecks_nearby_controller();
    std::cout << "multiblock_machine_service tests passed\n";
    return 0;
}
