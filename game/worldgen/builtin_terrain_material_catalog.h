// Built-in terrain material catalog for the game-owned world-generation module.
//
// The definitions were initially imported from the retired Godot content path,
// but this C++ catalog is now the runtime authority. It deliberately has no
// Godot, script VM, or engine registration dependency, so authoritative
// systems consume the same immutable WorldGenConfigSnapshot on client and
// dedicated-server hosts.

#pragma once

namespace snt::game {

struct WorldGenConfigSnapshot;

void register_builtin_terrain_material_catalog(WorldGenConfigSnapshot& config);

}  // namespace snt::game
