// Test-only immutable resource snapshot for player-state boundaries.
//
// Server player tests must inject an explicit published mapping instead of
// relying on an old dynamic item-ID allocator. Production sessions receive
// the GameContentRegistry snapshot during composition.

#pragma once

#include "game/resources/resource_runtime_index.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace snt::game::test_support {

[[nodiscard]] inline ResourceRuntimeIndex::Snapshot player_resource_snapshot() {
    static const ResourceRuntimeIndex::Snapshot snapshot = [] {
        const std::vector<ResourceContentKey> keys{
            ResourceContentKey::item("bed"),
            ResourceContentKey::item("bloomery"),
            ResourceContentKey::item("bone_meal"),
            ResourceContentKey::item("charcoal"),
            ResourceContentKey::item("coal"),
            ResourceContentKey::item("cobblestone"),
            ResourceContentKey::item("copper"),
            ResourceContentKey::item("crop.wheat"),
            ResourceContentKey::item("dirt"),
            ResourceContentKey::item("fire"),
            ResourceContentKey::item("hammer"),
            ResourceContentKey::item("iron"),
            ResourceContentKey::item("iron_chestplate"),
            ResourceContentKey::item("iron_ingot"),
            ResourceContentKey::item("iron_ore"),
            ResourceContentKey::item("iron_pickaxe"),
            ResourceContentKey::item("iron_bloom"),
            ResourceContentKey::item("relic"),
            ResourceContentKey::item("sand"),
            ResourceContentKey::item("seed.wheat"),
            ResourceContentKey::item("snow"),
            ResourceContentKey::item("steel_sword"),
            ResourceContentKey::item("stone"),
            ResourceContentKey::item("tiny_dust.stone"),
            ResourceContentKey::item("wooden_shovel"),
            ResourceContentKey::item("wrought_iron"),
        };
        ResourceRuntimeIndex index;
        const auto rebuilt = index.rebuild(keys);
        if (!rebuilt) {
            throw std::logic_error("Test player resource snapshot could not be built");
        }
        return index.snapshot();
    }();
    return snapshot;
}

}  // namespace snt::game::test_support
