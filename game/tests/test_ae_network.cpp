#include "game/automation/ae_network.h"
#include "game/automation/ae_storage_cell.h"
#include "game/resources/resource_runtime_index.h"

#include <gtest/gtest.h>

#include <vector>

namespace snt::game {
namespace {

[[nodiscard]] ResourceRuntimeIndex::Snapshot make_snapshot() {
    ResourceRuntimeIndex index;
    const std::vector<ResourceContentKey> keys{
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::item("copper.ingot"),
    };
    EXPECT_TRUE(index.rebuild(keys));
    return index.snapshot();
}

TEST(AeNetworkTopologyTest, KeepsNormalNodeAndChannelQueriesConstantTimeAfterRebuild) {
    AeNetworkTopology topology;
    const auto controller = topology.add_node({.type = AeNetworkNodeType::kController});
    const auto drive = topology.add_node({.type = AeNetworkNodeType::kDrive});
    const auto terminal = topology.add_node({.type = AeNetworkNodeType::kTerminal});
    ASSERT_TRUE(controller);
    ASSERT_TRUE(drive);
    ASSERT_TRUE(terminal);

    ASSERT_TRUE(topology.connect(*controller, *drive));
    ASSERT_TRUE(topology.connect(*drive, *terminal));
    const auto component = topology.component_of(*terminal);
    ASSERT_TRUE(component);
    const auto state = topology.component_state(*component);
    ASSERT_TRUE(state);
    EXPECT_TRUE(state->is_powered);
    EXPECT_EQ(state->total_channels, 32);
    EXPECT_EQ(state->online_devices, 2);
    EXPECT_TRUE(topology.is_online(*controller));
    EXPECT_TRUE(topology.is_online(*drive));
    EXPECT_TRUE(topology.is_online(*terminal));
    EXPECT_TRUE(topology.are_connected(*controller, *drive));

    ASSERT_TRUE(topology.disconnect(*drive, *terminal));
    ASSERT_TRUE(topology.component_of(*terminal).has_value());
    EXPECT_FALSE(topology.is_online(*terminal));
    EXPECT_FALSE(topology.are_connected(*drive, *terminal));
}

TEST(AeNetworkTopologyTest, AllocatesChannelsOnlyForDevicesAndFailsClosedWithoutOneController) {
    AeNetworkTopology topology;
    const auto controller = topology.add_node({.type = AeNetworkNodeType::kController});
    ASSERT_TRUE(controller);
    std::vector<AeNetworkNodeHandle> drives;
    for (int index = 0; index < 33; ++index) {
        const auto drive = topology.add_node({.type = AeNetworkNodeType::kDrive});
        ASSERT_TRUE(drive);
        ASSERT_TRUE(topology.connect(*controller, *drive));
        drives.push_back(*drive);
    }
    EXPECT_TRUE(topology.is_online(drives.front()));
    EXPECT_FALSE(topology.is_online(drives.back()));

    const auto second_controller = topology.add_node({.type = AeNetworkNodeType::kController});
    ASSERT_TRUE(second_controller);
    ASSERT_TRUE(topology.connect(*controller, *second_controller));
    EXPECT_FALSE(topology.is_online(*controller));
    EXPECT_FALSE(topology.is_online(drives.front()));
    const auto component = topology.component_of(*controller);
    ASSERT_TRUE(component);
    const auto state = topology.component_state(*component);
    ASSERT_TRUE(state);
    EXPECT_EQ(state->controller_count, 2u);
    EXPECT_FALSE(state->is_powered);
}

TEST(AeNetworkStorageIndexTest, TracksCellMutationsWithoutStorageScans) {
    const ResourceRuntimeIndex::Snapshot snapshot = make_snapshot();
    const ResourceKey iron = *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const ResourceKey copper = *snapshot.resolve_runtime(ResourceContentKey::item("copper.ingot"));
    auto created = AeStorageCell::create({
        .byte_capacity = 64,
        .max_distinct_resources = 4,
        .bytes_per_distinct_resource = 1,
        .units_per_byte = 1,
    }, snapshot);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);
    ASSERT_EQ(cell.insert(snapshot.key_context(), {.key = iron, .amount = 10},
                          ResourceTransferMode::kExecute),
              10);

    AeNetworkStorageIndex index(snapshot.key_context());
    const auto handle = index.attach_storage(
        cell.capture_runtime_contents(snapshot.key_context()));
    ASSERT_TRUE(handle);
    ASSERT_TRUE(cell.set_network_storage_observer(*handle, index));
    EXPECT_EQ(index.amount_of(snapshot.key_context(), iron), 10);
    EXPECT_EQ(index.amount_of(snapshot.key_context(), copper), 0);

    EXPECT_EQ(cell.insert(snapshot.key_context(), {.key = iron, .amount = 7},
                          ResourceTransferMode::kExecute),
              7);
    EXPECT_EQ(cell.insert(snapshot.key_context(), {.key = copper, .amount = 3},
                          ResourceTransferMode::kExecute),
              3);
    EXPECT_EQ(index.amount_of(snapshot.key_context(), iron), 17);
    EXPECT_EQ(index.amount_of(snapshot.key_context(), copper), 3);
    EXPECT_EQ(cell.extract(snapshot.key_context(), {.key = iron, .amount = 9},
                           ResourceTransferMode::kExecute),
              9);
    EXPECT_EQ(index.amount_of(snapshot.key_context(), iron), 8);

    cell.clear_network_storage_observer();
    EXPECT_TRUE(index.detach_storage(*handle));
    EXPECT_EQ(index.amount_of(snapshot.key_context(), iron), 0);
    EXPECT_EQ(index.storage_count(), 0u);
}

}  // namespace
}  // namespace snt::game
