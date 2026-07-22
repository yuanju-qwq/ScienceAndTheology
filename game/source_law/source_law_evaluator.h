// Deterministic source-law body evaluation.
//
// This API is intentionally value-only. A caller captures SourceLawBodyState
// and SourceLawEvaluationContext on the simulation main thread, evaluates on
// a worker if desired, then applies the returned values at its barrier.

#pragma once

#include "game/source_law/source_law_body_state.h"
#include "game/source_law/source_law_definition.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace snt::game::source_law {

struct SourceLawContributionReference {
    SourceOrganSlot slot = SourceOrganSlot::kCount;
    SourceLawId organ_definition_id;
    SourceLawElement element = SourceLawElement::kCount;
    ElementalPhysiologyAction action = ElementalPhysiologyAction::kCount;
    ElementalReactionStage stage = ElementalReactionStage::kCount;
    uint16_t contribution_index = 0;
};

struct ElementalReactionStepReport {
    SourceLawId step_id;
    bool is_satisfied = false;
    std::vector<SourceLawContributionReference> contributors;
};

struct ElementalReactionReport {
    SourceLawId reaction_id;
    bool is_continuous = false;
    bool growth_chain_continuous = false;
    bool all_byproducts_resolved = false;
    std::vector<ElementalReactionStepReport> closure_steps;
    std::vector<ElementalReactionStepReport> growth_steps;
    std::vector<SourceLawId> missing_step_ids;
    std::vector<SourceLawId> competing_step_ids;
    std::vector<SourceLawId> unresolved_byproduct_tags;
};

struct SourceLawSystemReport {
    SourceLawId system_id;
    SourceBodySystem body_system = SourceBodySystem::kCount;
    SourceLawSystemState state = SourceLawSystemState::kUnavailable;
    bool resonance_requirements_met = false;
    bool closure_requirements_met = false;
    bool growth_link_requirements_met = false;
    bool ecology_conditions_met = false;
    ElementalReactionReport reaction;
    std::vector<SourceOrganSlot> contributing_slots;
    std::vector<SourceLawId> blocking_reason_ids;
};

struct SourceCircuitScheduleReport {
    SourceCircuitSchedule effective_schedule;
    bool primary_circuit_is_valid = true;
    std::vector<SourceLawId> rejected_circuit_system_ids;
};

struct SourceLawPathReactionApplication {
    SourceBodySystem body_system = SourceBodySystem::kCount;
    ElementalReactionStage stage = ElementalReactionStage::kCount;
    ElementalPhysiologyAction action = ElementalPhysiologyAction::kCount;
    SourceLawId product_modifier_id;
    SourceLawId byproduct_handling_modifier_id;
};

struct SourceLawPathReport {
    SourceLawId active_path_id;
    bool definition_exists = true;
    bool core_organ_tags_met = false;
    bool preferred_system_is_closed = false;
    bool is_resonant = false;
    std::vector<SourceLawPathReactionApplication> applied_reaction_preferences;
    std::vector<SourceLawId> blocking_reason_ids;
};

struct SourceLawEvaluation {
    std::vector<SourceLawSystemReport> systems;
    SourceBodyIntegrationReport integration;
    SourceCircuitScheduleReport circuit_schedule;
    SourceLawPathReport path;
    float derived_source_throughput = 0.0F;
    int32_t derived_mana_max = 0;
    int32_t derived_mental_load = 0;
    std::vector<SourceLawId> body_blocking_reason_ids;
};

class SourceLawBodyEvaluator final {
public:
    [[nodiscard]] static SourceLawEvaluation evaluate(
        const SourceLawContentSnapshot& content,
        const SourceLawBodyState& body,
        const SourceLawEvaluationContext& context = {});

    // This is the only helper that copies derived evaluator output back to a
    // body value. It intentionally does not mutate any World/ECS component.
    [[nodiscard]] static SourceLawBodyState apply_evaluation(
        SourceLawBodyState body, const SourceLawEvaluation& evaluation);
};

// Keeps state-transition logging outside the pure evaluator. Call this only
// after an authoritative transaction has committed a returned body value.
class SourceLawBodyTransitionLogger final {
public:
    static void log_if_changed(std::string_view subject_id,
                               const SourceLawEvaluation& previous,
                               const SourceLawEvaluation& current);
};

}  // namespace snt::game::source_law
