// Game-owned physical and instance resource state contracts.
//
// ResourceKey identifies a stackable resource and ResourceStack carries only
// its quantity. This module owns value types that must never participate in
// key equality: item durability/instance payloads and fluid temperature,
// pressure, flow, and mixture. Voxel terrain retains a separate CellFluidId
// projection and cell-local state for dense solver access.

#pragma once

#include "game/resources/resource_key.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace snt::game {

// One singular item's mutable state. A non-empty instance is valid only with
// a ResourceStack amount of one; stack/inventory owners enforce that rule.
struct ItemInstance {
    // -1 means this item definition has no tracked durability state.
    int32_t durability = -1;
    std::string schema_id;
    std::vector<std::byte> payload;

    [[nodiscard]] bool empty() const noexcept {
        return durability == -1 && schema_id.empty() && payload.empty();
    }
    [[nodiscard]] bool is_valid() const noexcept {
        return durability >= -1 &&
               (schema_id.empty() ? payload.empty() : true);
    }
};

// One material component of a mixed fluid state. The proportions of a
// non-empty mixture must sum to one million parts per million.
struct FluidMixtureComponent {
    ResourceKey key;
    uint32_t proportion_ppm = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return key.is_valid() && proportion_ppm != 0;
    }
};

// Container, pipe, or packet-local fluid conditions. A pure ResourceStack
// has an empty mixture; a non-empty mixture describes physical composition
// without changing the generic resource identity.
struct FluidState {
    int16_t temperature_kelvin = 300;
    int32_t pressure_pascal = 101'325;
    int32_t flow_millibuckets_per_tick = 0;
    std::vector<FluidMixtureComponent> mixture;

    [[nodiscard]] bool is_valid() const noexcept {
        if (temperature_kelvin <= 0 || pressure_pascal < 0) return false;
        if (mixture.empty()) return true;
        uint64_t total = 0;
        for (const FluidMixtureComponent& component : mixture) {
            if (!component.is_valid()) return false;
            total += component.proportion_ppm;
        }
        return total == 1'000'000;
    }
};

}  // namespace snt::game
