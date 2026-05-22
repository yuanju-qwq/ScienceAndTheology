#pragma once

#include <cstdint>
#include <string>
#include <functional>

namespace science_and_theology {

// Unique identifier for entities, machines, and connectors.
// Wraps a uint64_t; 0 means invalid/unassigned.
// Supports hashing for unordered_map usage.
struct EntityId {
    uint64_t id = 0;

    bool operator==(const EntityId& other) const { return id == other.id; }
    bool operator!=(const EntityId& other) const { return id != other.id; }
    bool operator<(const EntityId& other) const { return id < other.id; }

    bool is_valid() const { return id != 0; }
    explicit operator bool() const { return is_valid(); }
};

// Aliases for type-safe usage. Underlying type is the same uint64_t.
using MachineId = EntityId;
using ConnectorId = EntityId;

// Placement data for a game entity spawned in a chunk.
// Lightweight snapshot; full runtime state lives in the entity registry.
struct EntityPlacement {
    EntityId id;
    std::string type_name;
    int cell_x = 0;
    int cell_y = 0;
};

// Placement data for an interactive machine placed in a chunk.
struct MachinePlacement {
    MachineId id;
    std::string type_name;
    int cell_x = 0;
    int cell_y = 0;
};

} // namespace science_and_theology

// std::hash specialization for EntityId to use in unordered_map/unordered_set.
template <>
struct std::hash<science_and_theology::EntityId> {
    size_t operator()(const science_and_theology::EntityId& e) const {
        return std::hash<uint64_t>()(e.id);
    }
};