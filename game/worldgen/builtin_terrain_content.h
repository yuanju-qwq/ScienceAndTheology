// Built-in game-owned terrain content registration.
//
// This module owns the semantic terrain catalog and related world-generation
// content. It is intentionally independent from Godot and script VMs.

#pragma once

#include "core/expected.h"
#include "game/worldgen/world_gen_config.h"

namespace snt::game {

// Input used to derive the material, biome, rock-layer, and ore-vein rules
// for one spherical planet. Callers populate this from their game-owned
// universe data before publishing the immutable WorldGenConfigSnapshot.
struct BuiltinTerrainPlanetInput {
    PlanetConfig planet;
    float gravity_multiplier = 1.0f;
};

// Registers static built-in terrain definitions into an unfinalized config
// draft. It adds material semantics, semantic role bindings, tree species, and
// crop species. Call finalize_world_gen_config() after all material producers
// have completed registration.
void register_builtin_terrain_content(WorldGenConfigSnapshot& config);

// Adds the rules derived from one planet after material-key finalization. This
// mutates a draft only; callers must not invoke it on a published snapshot.
[[nodiscard]] snt::core::Expected<void> append_builtin_terrain_planet_content(
    WorldGenConfigSnapshot& config, const BuiltinTerrainPlanetInput& input);

}  // namespace snt::game
