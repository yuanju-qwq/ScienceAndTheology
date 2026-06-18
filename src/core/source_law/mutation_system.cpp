#include "mutation_system.hpp"

#include <cmath>
#include <random>

namespace science_and_theology::source_law {

MutationStage MutationSystem::get_stage(float mutation) {
    if (mutation <= 20.0f) return MutationStage::NORMAL;
    if (mutation <= 40.0f) return MutationStage::MILD_POLLUTION;
    if (mutation <= 60.0f) return MutationStage::SYMPTOMS;
    if (mutation <= 80.0f) return MutationStage::SEVERE_MUTATION;
    return MutationStage::SOURCE_RUNAWAY;
}

MutationStageEffects MutationSystem::get_stage_effects(MutationStage stage) {
    switch (stage) {
        case MutationStage::NORMAL:
            return {1.0f, 1.0f, 0.0f, 0.0f, false};
        case MutationStage::MILD_POLLUTION:
            return {1.1f, 0.9f, 0.05f, 0.0f, false};
        case MutationStage::SYMPTOMS:
            return {1.25f, 0.75f, 0.15f, 1.0f, false};
        case MutationStage::SEVERE_MUTATION:
            return {1.5f, 0.5f, 0.3f, 3.0f, false};
        case MutationStage::SOURCE_RUNAWAY:
            return {2.0f, 0.2f, 0.5f, 5.0f, true};
        default:
            return {1.0f, 1.0f, 0.0f, 0.0f, false};
    }
}

void MutationSystem::tick_stability_mutation(PlayerSourceLawData& data) {
    if (!data.is_initiated()) return;

    auto report = data.compute_network_report();

    // --- Target-convergence model ---
    // Organ network determines a target stability and mutation.
    // Current values converge toward the target each tick.
    //
    // Target stability = 100 + element_stability_modifier * scale
    // Target mutation  =   0 + element_mutation_modifier  * scale
    // Per tick: delta = (target - current) * rate

    // Stability: converge toward target.
    float target_stability = kMaxStability
        + report.element_stability_modifier * kStabilityTargetScale;
    target_stability = std::clamp(target_stability, kMinStability, kMaxStability);
    float stability_delta = (target_stability - data.stability())
        * kStabilityConvergenceRate;
    data.modify_stability(stability_delta);

    // Mutation: converge toward target.
    float target_mutation = 0.0f
        + report.element_mutation_modifier * kMutationTargetScale;
    target_mutation = std::clamp(target_mutation, 0.0f, kMaxMutation);
    float mutation_delta = (target_mutation - data.mutation())
        * kMutationConvergenceRate;

    // Severe conflict adds extra mutation drift (additive).
    if (report.has_severe_conflict) {
        mutation_delta += kSevereConflictMutationBonus;
    }

    // Low stability adds extra mutation drift (additive).
    if (data.stability() < kLowStabilityThreshold) {
        mutation_delta += (kLowStabilityThreshold - data.stability())
            * kLowStabilityMutationRate;
    }

    // High stability naturally reduces mutation (additive).
    if (data.stability() > kHighStabilityThreshold && data.mutation() > 0.0f) {
        mutation_delta -= (data.stability() - kHighStabilityThreshold)
            * kHighStabilityMutationReductionRate;
    }

    data.modify_mutation(mutation_delta);

    // Mental load from mutation.
    auto stage = get_stage(data.mutation());
    auto effects = get_stage_effects(stage);
    if (effects.mental_load_increase > 0.0f) {
        data.modify_mental_load(static_cast<int>(effects.mental_load_increase));
    }
}

bool MutationSystem::should_trigger_mutation_death(const PlayerSourceLawData& data) {
    if (data.mutation() < 80.0f) return false;

    auto stage = get_stage(data.mutation());
    if (stage != MutationStage::SOURCE_RUNAWAY) return false;

    // Probability increases with mutation above 80.
    float excess = data.mutation() - 80.0f;
    float chance = excess / 200.0f;

    static std::mt19937 rng(42);
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng) < chance;
}

void MutationSystem::handle_mutation_death(PlayerSourceLawData& data) {
    data.purify_all_organs();
    data.set_mutation(0.0f);
    data.set_stability(kDefaultStability);
    data.set_mental_load(0);
    data.set_psionic_level(0);
    data.enforce_mortal_mana_rule();
}

} // namespace science_and_theology::source_law
