// ScienceAndTheology gameplay ECS components.
//
// These persistent value types belong to the game host, never snt_ecs. They
// are safe to use in game systems and saves, but engine modules must not
// include this header or attach gameplay meaning to their generic World.

#pragma once

#include "core/binary_reader.h"
#include "core/binary_writer.h"
#include "core/serializer.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace snt::game {

struct Health {
    float current = 1.0f;
    float maximum = 1.0f;

    bool is_dead() const { return current <= 0.0f; }
    float fraction() const { return maximum > 0.0f ? current / maximum : 0.0f; }
};

struct Inventory {
    struct Slot {
        std::string item_key;
        int32_t count = 0;
    };

    std::vector<Slot> slots;
    int32_t max_slots = 16;
};

struct PlayerMarker {};
struct CreatureMarker {};
struct StaticMarker {};

}  // namespace snt::game

namespace snt::core {

template <>
struct Serializer<snt::game::Health> {
    static void write(BinaryWriter& writer, const snt::game::Health& health) {
        writer.write_f32(health.current);
        writer.write_f32(health.maximum);
    }

    static bool read(BinaryReader& reader, snt::game::Health& health) {
        return reader.read_f32(health.current) && reader.read_f32(health.maximum);
    }
};

template <>
struct Serializer<snt::game::Inventory> {
    static void write(BinaryWriter& writer, const snt::game::Inventory& inventory) {
        writer.write_i32(inventory.max_slots);
        writer.write_u32(static_cast<uint32_t>(inventory.slots.size()));
        for (const auto& slot : inventory.slots) {
            writer.write_string(slot.item_key);
            writer.write_i32(slot.count);
        }
    }

    static bool read(BinaryReader& reader, snt::game::Inventory& inventory) {
        if (!reader.read_i32(inventory.max_slots)) return false;

        uint32_t slot_count = 0;
        if (!reader.read_u32(slot_count)) return false;

        inventory.slots.clear();
        inventory.slots.reserve(slot_count);
        for (uint32_t index = 0; index < slot_count; ++index) {
            snt::game::Inventory::Slot slot;
            if (!reader.read_string(slot.item_key) || !reader.read_i32(slot.count)) {
                return false;
            }
            inventory.slots.push_back(std::move(slot));
        }
        return true;
    }
};

}  // namespace snt::core
