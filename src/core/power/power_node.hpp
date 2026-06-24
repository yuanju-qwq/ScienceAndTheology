#pragma once

#include <cstdint>

#include "../config/gt_values.hpp"

namespace science_and_theology::gt {

// ============================================================
// MapPosition — 3D voxel coordinate (engine-independent)
// ============================================================
//
// Shared by all per-block network systems (power, signal, fluid,
// item pipes, SFM cables). Godot adapters convert Vector3i to/from
// this type. Kept here as the canonical home; power_node.hpp was the
// original location and the include path is preserved to avoid
// churn across the many dependents (fluid/item_pipe/sfm networks).

struct MapPosition {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const MapPosition& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator!=(const MapPosition& other) const {
        return !(*this == other);
    }

    // Lexicographic ordering for std::sort / std::set usage.
    bool operator<(const MapPosition& other) const {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
};

} // namespace science_and_theology::gt

// Hash specialization for MapPosition so it can be used as unordered_map key.
namespace std {
template <>
struct hash<science_and_theology::gt::MapPosition> {
    size_t operator()(const science_and_theology::gt::MapPosition& p) const noexcept {
        // 64-bit FNV-style combine of three 32-bit coordinates.
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](uint32_t v) {
            h ^= v;
            h *= 1099511628211ULL;
        };
        mix(static_cast<uint32_t>(p.x));
        mix(static_cast<uint32_t>(p.y));
        mix(static_cast<uint32_t>(p.z));
        return static_cast<size_t>(h);
    }
};
} // namespace std

namespace science_and_theology::gt {

// ============================================================
// Overload reporting (shared by power network)
// ============================================================

// Possible overload result states for a cable block or attached machine.
enum class OverloadState : uint8_t {
    OK = 0,
    OVER_VOLTAGE,   // Machine receiving power above its max voltage
    OVER_CAPACITY,  // Cable carrying more current than its capacity
};

// Reasons why a cable or machine is overloaded.
struct OverloadInfo {
    OverloadState state = OverloadState::OK;
    int64_t actual_load = 0;
    int64_t max_capacity = 0;
    int64_t actual_voltage = 0;
    int64_t max_voltage = 0;
};

} // namespace science_and_theology::gt
