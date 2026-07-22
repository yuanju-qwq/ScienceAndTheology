#include "game/simulation/automation_controller_persistence.h"
#include "game/world/save/chunk_serializer.h"

#include <gtest/gtest.h>

namespace snt::game {
namespace {

[[nodiscard]] SfmFlowProgramRecord interval_program(uint64_t revision = 7) {
    return {
        .revision = revision,
        .nodes = {{.id = 1, .type = SfmFlowNodeType::kInterval, .interval_ticks = 20}},
    };
}

TEST(AutomationControllerPersistenceTest, CreatesTypedAnchorAndReplacesOnlyProgramState) {
    const ChunkKey key{"overworld", -1, 0, 2};
    GameChunkSidecarRegistry sidecars;
    sidecars.set(key, {});
    const auto created = GameAutomationControllerPersistence::create_controller(
        sidecars, key, -1, 4, VoxelChunk::kChunkSize * 2,
        {.kind = AutomationControllerKind::kSfmManager,
         .controller_key = std::string(kSfmManagerControllerKey),
         .sfm_program = interval_program()});
    ASSERT_TRUE(created);
    ASSERT_TRUE(GameAutomationControllerPersistence::validate_all(sidecars));

    const GameChunkSidecar* const sidecar = sidecars.get(key);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->block_entities.size(), 1u);
    EXPECT_EQ(sidecar->block_entities.front().id, created->anchor_entity_id);
    EXPECT_EQ(sidecar->block_entities.front().entity_type,
              BlockEntityType::AUTOMATION_CONTROLLER);
    ASSERT_EQ(sidecar->automation_controller_records.size(), 1u);
    EXPECT_EQ(sidecar->automation_controller_records.front().controller_key,
              kSfmManagerControllerKey);
    EXPECT_EQ(sidecar->automation_controller_records.front().sfm_program.revision, 7u);

    auto replacement = interval_program(0);
    replacement.nodes.front().interval_ticks = 40;
    auto replaced_program = GameAutomationControllerPersistence::replace_sfm_program(
        sidecars, created->anchor_entity_id, 7,
        {.nodes = std::move(replacement.nodes),
         .connections = std::move(replacement.connections)});
    ASSERT_TRUE(replaced_program);
    EXPECT_EQ(replaced_program->chunk_key, key);
    EXPECT_EQ(replaced_program->sfm_program.revision, 8u);
    const GameChunkSidecar* const replaced = sidecars.get(key);
    ASSERT_NE(replaced, nullptr);
    EXPECT_EQ(replaced->automation_controller_records.front().revision, 2u);
    EXPECT_EQ(replaced->automation_controller_records.front().sfm_program.revision, 8u);
    EXPECT_EQ(replaced->automation_controller_records.front().sfm_program.nodes.front().interval_ticks,
              40u);
    EXPECT_FALSE(GameAutomationControllerPersistence::replace_sfm_program(
        sidecars, created->anchor_entity_id, 7,
        {.nodes = {{.id = 1, .type = SfmFlowNodeType::kInterval, .interval_ticks = 60}}}));

    ASSERT_TRUE(GameAutomationControllerPersistence::remove_controller(
        sidecars, created->anchor_entity_id));
    const GameChunkSidecar* const removed = sidecars.get(key);
    ASSERT_NE(removed, nullptr);
    EXPECT_TRUE(removed->automation_controller_records.empty());
    EXPECT_TRUE(removed->block_entities.empty());
}

TEST(AutomationControllerPersistenceTest, RejectsRootOutsideOwningChunk) {
    const ChunkKey key{"overworld", 0, 0, 0};
    GameChunkSidecarRegistry sidecars;
    sidecars.set(key, {});
    EXPECT_FALSE(GameAutomationControllerPersistence::create_controller(
        sidecars, key, VoxelChunk::kChunkSize, 0, 0,
        {.kind = AutomationControllerKind::kSfmManager,
         .controller_key = std::string(kSfmManagerControllerKey),
         .sfm_program = interval_program()}));
}

TEST(GameChunkSerializerTest, RoundTripsAutomationControllerSidecar) {
    GameChunk original;
    original.chunk_x = 0;
    original.chunk_y = 0;
    original.chunk_z = 0;
    original.terrain.resize(1, 1, 1);
    original.block_entities.push_back({
        .id = EntityId{(uint64_t{1} << 61u) | 1u},
        .entity_type = BlockEntityType::AUTOMATION_CONTROLLER,
        .root_x = 0,
        .root_y = 0,
        .root_z = 0,
        .owned_cell_count = 1,
    });
    original.automation_controller_records.push_back({
        .anchor_entity_id = original.block_entities.front().id,
        .kind = AutomationControllerKind::kSfmManager,
        .controller_key = std::string(kSfmManagerControllerKey),
        .revision = 4,
        .sfm_program = interval_program(11),
    });

    const GameChunkSerializer serializer;
    const auto payload = serializer.serialize("overworld", original);
    GameChunk restored;
    std::string dimension;
    ASSERT_TRUE(serializer.deserialize(payload, dimension, restored));
    EXPECT_EQ(dimension, "overworld");
    ASSERT_EQ(restored.automation_controller_records.size(), 1u);
    const AutomationControllerPersistenceRecord& record =
        restored.automation_controller_records.front();
    EXPECT_EQ(record.anchor_entity_id, original.block_entities.front().id);
    EXPECT_EQ(record.kind, AutomationControllerKind::kSfmManager);
    EXPECT_EQ(record.controller_key, kSfmManagerControllerKey);
    EXPECT_EQ(record.revision, 4u);
    EXPECT_EQ(record.sfm_program.revision, 11u);
    ASSERT_EQ(record.sfm_program.nodes.size(), 1u);
    EXPECT_EQ(record.sfm_program.nodes.front().interval_ticks, 20u);
}

}  // namespace
}  // namespace snt::game
