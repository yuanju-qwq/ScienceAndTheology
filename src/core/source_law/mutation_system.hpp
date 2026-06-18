#pragma once

#include <cstdint>

#include "player_source_law_data.hpp"

namespace science_and_theology::source_law {

// ============================================================
// MutationSystem — handles stability/mutation per-tick logic
// ============================================================
//
// Mutation stages (from design doc 7.3):
// 0-20:  Normal — no side effects
// 21-40: Mild pollution — occasional vision distortion, source regen fluctuation
// 41-60: Mutation symptoms — organ side effects, spell offset
// 61-80: Severe mutation — skill loss of control, mental load increase
// 81-100: Source runaway — possible death, berserk, or generate mutant

enum class MutationStage : uint8_t {
    NORMAL = 0,
    MILD_POLLUTION,
    SYMPTOMS,
    SEVERE_MUTATION,
    SOURCE_RUNAWAY,
    COUNT
};

struct MutationStageEffects {
    float mana_cost_multiplier = 1.0f;
    float source_regen_multiplier = 1.0f;
    float skill_failure_chance = 0.0f;
    float mental_load_increase = 0.0f;
    bool can_trigger_death = false;
};

class MutationSystem {
public:
    // Get current mutation stage from mutation value.
    static MutationStage get_stage(float mutation);

    // Get effects for a given mutation stage.
    static MutationStageEffects get_stage_effects(MutationStage stage);

    // Compute per-tick stability and mutation changes.
    // Should be called once per game tick.
    static void tick_stability_mutation(PlayerSourceLawData& data);

    // Check if mutation death should trigger.
    static bool should_trigger_mutation_death(const PlayerSourceLawData& data);

    // Handle mutation death: purify all organs, reset mutation.
    static void handle_mutation_death(PlayerSourceLawData& data);
};

} // namespace science_and_theology::source_law
