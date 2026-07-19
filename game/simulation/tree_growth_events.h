// Narrow game-owned contracts for authoritative tree-growth mutations.
//
// Tree growth runs in the shared simulation target, while server replication
// and future climate implementations stay at their respective composition
// boundaries. These value-only contracts deliberately expose no mutable
// chunk, sidecar, WorldData, or transport object.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace snt::game {

struct TreeGrowthTerrainChange {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    uint32_t previous_material = 0;
    uint32_t previous_flags = 0;
    uint32_t current_material = 0;
    uint32_t current_flags = 0;
};

class ITreeGrowthMutationSink {
public:
    virtual ~ITreeGrowthMutationSink() = default;
    virtual void on_tree_growth_terrain_changed(
        const TreeGrowthTerrainChange& change) = 0;
};

// A climate or ecosystem module can provide a deterministic sample when it
// exists. Until then, tree growth intentionally has no fabricated noise-based
// climate dependency; an unavailable sample means only timing and terrain
// conditions are evaluated.
struct TreeGrowthEnvironment {
    float temperature = 0.0f;
    float humidity = 0.0f;
};

class ITreeGrowthEnvironmentProvider {
public:
    virtual ~ITreeGrowthEnvironmentProvider() = default;
    virtual bool sample_tree_growth_environment(
        std::string_view dimension_id,
        int32_t block_x,
        int32_t block_y,
        int32_t block_z,
        TreeGrowthEnvironment& out_environment) const = 0;
};

}  // namespace snt::game
