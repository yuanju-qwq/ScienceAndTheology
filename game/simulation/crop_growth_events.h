// Narrow game-owned contracts for authoritative crop-growth mutations.
//
// Crop simulation is shared by client-local and server runtimes, while AOI
// replication and a future weather/ecology implementation remain composition
// concerns. These value-only contracts deliberately expose neither mutable
// chunks nor legacy WorldData state.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace snt::game {

struct CropGrowthTerrainChange {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    uint32_t previous_material = 0;
    uint32_t previous_flags = 0;
    uint32_t current_material = 0;
    uint32_t current_flags = 0;
};

class ICropGrowthMutationSink {
public:
    virtual ~ICropGrowthMutationSink() = default;
    virtual void on_crop_growth_terrain_changed(
        const CropGrowthTerrainChange& change) = 0;
};

// A future deterministic climate/ecology system can publish one sample for a
// crop or farmland cell. Until then, unavailable samples intentionally do not
// recreate the legacy noise model: planted crops use their persistent farmland
// values, and climate suitability is not fabricated.
struct CropGrowthEnvironment {
    float temperature = 0.0f;
    float humidity = 0.0f;
    float soil_fertility = 0.5f;
    float water_availability = 0.5f;
    bool is_raining = false;
};

class ICropGrowthEnvironmentProvider {
public:
    virtual ~ICropGrowthEnvironmentProvider() = default;
    virtual bool sample_crop_growth_environment(
        std::string_view dimension_id,
        int32_t block_x,
        int32_t block_y,
        int32_t block_z,
        CropGrowthEnvironment& out_environment) const = 0;
};

}  // namespace snt::game
