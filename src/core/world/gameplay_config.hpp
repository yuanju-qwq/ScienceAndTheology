#pragma once

#include <string>
#include <unordered_map>

namespace science_and_theology {

// Runtime gameplay configuration, separate from world generation config.
// Controls gameplay systems that operate at runtime (collapse, gravity fall, etc.).
// Can be modified at runtime without regenerating the world.
//
// Design principle: WorldGenConfigSnapshot is frozen and read-only for worker threads.
// GameplayConfig is mutable and only accessed on the main thread or under mutex.
struct GameplayConfig {
    // --- Collapse system ---

    // Master switch for the cave-in / collapse system.
    // When false, no collapse checks are performed after mining.
    bool enable_collapse = true;

    // Global multiplier applied to each material's collapse_chance.
    // 0.0 = never collapse, 1.0 = normal, 2.0 = very unstable.
    float collapse_chance_multiplier = 1.0f;

    // Maximum number of blocks that can collapse in a single chain reaction.
    // Prevents infinite or excessively large cave-ins.
    int max_collapse_chain = 64;

    // --- Support beam ---

    // Radius (in blocks) within which a support beam prevents collapse.
    // Measured along the gravity direction (not diagonal).
    int support_beam_radius = 5;

    // --- Gravity fall system ---

    // Master switch for gravity-affected blocks (sand, gravel).
    // When false, TF_GRAVITY_FALL blocks behave like normal solid blocks.
    bool enable_gravity_fall = true;

    // Maximum number of blocks that can fall in a single chain reaction.
    // Prevents infinite sand column collapse.
    int max_gravity_fall_chain = 64;

    // --- Per-planet overrides ---

    // Per-dimension gameplay config overrides. If a dimension has an entry
    // here, its values take precedence over the global defaults.
    // Missing fields fall back to the global config.
    struct PlanetOverride {
        bool has_enable_collapse = false;
        bool enable_collapse = true;

        bool has_collapse_chance_multiplier = false;
        float collapse_chance_multiplier = 1.0f;

        bool has_max_collapse_chain = false;
        int max_collapse_chain = 64;

        bool has_support_beam_radius = false;
        int support_beam_radius = 5;

        bool has_enable_gravity_fall = false;
        bool enable_gravity_fall = true;

        bool has_max_gravity_fall_chain = false;
        int max_gravity_fall_chain = 64;
    };

    std::unordered_map<std::string, PlanetOverride> planet_overrides;

    // --- Resolved accessors (apply planet override if present) ---

    bool is_collapse_enabled(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_enable_collapse) {
            return it->second.enable_collapse;
        }
        return enable_collapse;
    }

    float get_collapse_chance_multiplier(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_collapse_chance_multiplier) {
            return it->second.collapse_chance_multiplier;
        }
        return collapse_chance_multiplier;
    }

    int get_max_collapse_chain(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_max_collapse_chain) {
            return it->second.max_collapse_chain;
        }
        return max_collapse_chain;
    }

    int get_support_beam_radius(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_support_beam_radius) {
            return it->second.support_beam_radius;
        }
        return support_beam_radius;
    }

    bool is_gravity_fall_enabled(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_enable_gravity_fall) {
            return it->second.enable_gravity_fall;
        }
        return enable_gravity_fall;
    }

    int get_max_gravity_fall_chain(const std::string& dimension_id) const {
        auto it = planet_overrides.find(dimension_id);
        if (it != planet_overrides.end() && it->second.has_max_gravity_fall_chain) {
            return it->second.max_gravity_fall_chain;
        }
        return max_gravity_fall_chain;
    }
};

} // namespace science_and_theology
