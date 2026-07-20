// Migrated legacy terrain material catalog.
//
// This is game-owned static content. It deliberately has no Godot, script VM,
// or engine registration dependency, so authoritative systems can consume the
// same immutable WorldGenConfigSnapshot on client and dedicated-server hosts.

#pragma once

namespace snt::game {

struct WorldGenConfigSnapshot;

void register_migrated_legacy_terrain_material_catalog(WorldGenConfigSnapshot& config);

}  // namespace snt::game
