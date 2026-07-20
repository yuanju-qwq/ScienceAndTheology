// Coverage for the game-owned typed region topology boundary.

#include "game/simulation/region_topology.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace {

class RegionEventRecorder final : public snt::game::IRegionTopologyEventSink {
public:
    void on_region_topology_event(const snt::game::RegionTopologyEvent& event) override {
        events.push_back(event);
    }

    std::vector<snt::game::RegionTopologyEvent> events;
};

size_t count_events(const RegionEventRecorder& recorder,
                    snt::game::RegionTopologyEventKind kind) {
    return static_cast<size_t>(std::count_if(
        recorder.events.begin(), recorder.events.end(), [kind](const auto& event) {
            return event.kind == kind;
        }));
}

}  // namespace

TEST(GameRegionTopologyTest, ReconcilesExplicitTopologyAndPublishesLifecycleEvents) {
    snt::game::RegionTopology topology;
    RegionEventRecorder recorder;
    topology.set_event_sink(&recorder);

    const snt::game::RegionNode first{"overworld", 0, 0, 0};
    const snt::game::RegionNode middle{"overworld", 1, 0, 0};
    const snt::game::RegionNode last{"overworld", 2, 0, 0};
    const auto first_region = topology.add_node(snt::game::RegionDomain::kPower, first);
    const auto middle_region = topology.add_node(snt::game::RegionDomain::kPower, middle);
    const auto last_region = topology.add_node(snt::game::RegionDomain::kPower, last);
    ASSERT_TRUE(first_region.valid());
    ASSERT_TRUE(middle_region.valid());
    ASSERT_TRUE(last_region.valid());

    const auto merged = topology.connect(snt::game::RegionDomain::kPower, first, middle);
    ASSERT_TRUE(merged.valid());
    ASSERT_TRUE(topology.connect(snt::game::RegionDomain::kPower, middle, last).valid());
    ASSERT_EQ(topology.region_count(snt::game::RegionDomain::kPower), 1u);
    ASSERT_EQ(topology.find_state(merged)->node_count, 3u);

    ASSERT_TRUE(topology.remove_node(snt::game::RegionDomain::kPower, middle));
    EXPECT_TRUE(topology.fixed_tick(1));
    EXPECT_EQ(topology.region_count(snt::game::RegionDomain::kPower), 2u);
    const auto first_after_split = topology.region_for_node(
        snt::game::RegionDomain::kPower, first);
    const auto last_after_split = topology.region_for_node(
        snt::game::RegionDomain::kPower, last);
    ASSERT_TRUE(first_after_split.has_value());
    ASSERT_TRUE(last_after_split.has_value());
    EXPECT_NE(first_after_split->id, last_after_split->id);
    EXPECT_EQ(count_events(recorder, snt::game::RegionTopologyEventKind::kCreated), 3u);
    EXPECT_EQ(count_events(recorder, snt::game::RegionTopologyEventKind::kMerged), 2u);
    EXPECT_EQ(count_events(recorder, snt::game::RegionTopologyEventKind::kSplit), 1u);

    ASSERT_TRUE(topology.remove_node(snt::game::RegionDomain::kPower, first));
    EXPECT_EQ(topology.region_count(snt::game::RegionDomain::kPower), 1u);
    EXPECT_EQ(count_events(recorder, snt::game::RegionTopologyEventKind::kDestroyed), 1u);
}

TEST(GameRegionTopologyTest, ReconcilesExplicitEdgeRemovalOnNextTick) {
    snt::game::RegionTopology topology;
    const snt::game::RegionNode first{"overworld", 0, 0, 0};
    const snt::game::RegionNode second{"overworld", 1, 0, 0};
    ASSERT_TRUE(topology.add_node(snt::game::RegionDomain::kFluid, first).valid());
    ASSERT_TRUE(topology.add_node(snt::game::RegionDomain::kFluid, second).valid());
    ASSERT_TRUE(topology.connect(snt::game::RegionDomain::kFluid, first, second).valid());

    ASSERT_TRUE(topology.disconnect(snt::game::RegionDomain::kFluid, first, second));
    EXPECT_EQ(topology.region_count(snt::game::RegionDomain::kFluid), 1u);
    ASSERT_TRUE(topology.fixed_tick(1));
    EXPECT_EQ(topology.region_count(snt::game::RegionDomain::kFluid), 2u);
}

TEST(GameRegionTopologyTest, DiffusesPollutionOncePerAuthoritativeTick) {
    snt::game::RegionTopology topology;
    const snt::game::RegionNode source{"overworld", 0, 0, 0};
    const snt::game::RegionNode target{"overworld", 1, 0, 0};
    const auto source_region = topology.add_node(
        snt::game::RegionDomain::kPollution, source);
    const auto target_region = topology.add_node(
        snt::game::RegionDomain::kPollution, target);
    ASSERT_TRUE(topology.set_pollution(source_region, 1.0));
    ASSERT_TRUE(topology.set_pollution(target_region, 0.0));

    EXPECT_FALSE(topology.set_pollution(
        topology.add_node(snt::game::RegionDomain::kPower, {"overworld", 8, 0, 0}), 0.5));
    ASSERT_TRUE(topology.fixed_tick(7));
    EXPECT_NEAR(topology.find_state(source_region)->pollution, 0.95, 0.000001);
    EXPECT_NEAR(topology.find_state(target_region)->pollution, 0.05, 0.000001);

    EXPECT_FALSE(topology.fixed_tick(7));
    EXPECT_NEAR(topology.find_state(source_region)->pollution, 0.95, 0.000001);
    EXPECT_NEAR(topology.find_state(target_region)->pollution, 0.05, 0.000001);

    ASSERT_TRUE(topology.fixed_tick(8));
    EXPECT_NEAR(topology.find_state(source_region)->pollution, 0.905, 0.000001);
    EXPECT_NEAR(topology.find_state(target_region)->pollution, 0.095, 0.000001);
    ASSERT_TRUE(topology.last_fixed_tick().has_value());
    EXPECT_EQ(*topology.last_fixed_tick(), 8u);
}
