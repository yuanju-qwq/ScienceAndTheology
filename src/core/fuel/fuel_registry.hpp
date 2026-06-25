#pragma once

#include <cstdint>
#include <vector>

#include "fuel_def.hpp"

namespace science_and_theology::gt {

class FuelRegistry {
public:
    // initialize = reset（Fuel 没有 ID 0 概念，用 index 存储）。
    static void initialize();
    // 完全清空 registry（用于热重载）。
    static void reset();
    static void register_fuel(const FuelDefinition& def);

    static const FuelDefinition* get_by_item(ItemId item_id);
    static const FuelDefinition* get_by_fluid(FluidId fluid_id);

    static bool is_item_fuel(ItemId item_id);
    static bool is_fluid_fuel(FluidId fluid_id);

    static int64_t get_item_burn_ticks(ItemId item_id);
    static int64_t get_fluid_burn_ticks(FluidId fluid_id);

    static const std::vector<FuelDefinition>& get_all();
    static size_t get_fuel_count();

private:
    static std::vector<FuelDefinition>& registry();
};

} // namespace science_and_theology::gt
