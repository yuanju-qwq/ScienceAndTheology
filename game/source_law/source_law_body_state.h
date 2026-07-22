// Shared runtime state for player and creature source-law bodies.

#pragma once

#include "game/source_law/source_law_spell_graph.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace snt::game::source_law {

struct OrganInstance {
    SourceLawId definition_id;
    uint16_t growth_level = 0;
    SourceLawId quality_id;
    float contamination = 0.0F;
    float integrity = 1.0F;
    std::vector<SourceLawId> tuning_tags;

    friend bool operator==(const OrganInstance&, const OrganInstance&) = default;
};

struct SourceCircuitSchedule {
    std::optional<SourceLawId> current_primary_circuit_system_id;
    std::vector<SourceLawId> coordinating_circuit_system_ids;
    float primary_circuit_reallocation_cooldown_seconds = 0.0F;

    friend bool operator==(const SourceCircuitSchedule&, const SourceCircuitSchedule&) = default;
};

struct SourceBodyIntegrationBridgeReport {
    SourceBodyIntegrationBridge bridge = SourceBodyIntegrationBridge::kCount;
    bool is_satisfied = false;
    std::vector<SourceLawId> blocking_reason_ids;

    friend bool operator==(const SourceBodyIntegrationBridgeReport&,
                           const SourceBodyIntegrationBridgeReport&) = default;
};

struct SourceBodyIntegrationReport {
    SourceBodyStage stage = SourceBodyStage::kDormant;
    bool all_slots_sublimated = false;
    bool energy_and_refinement_bridge = false;
    bool control_and_feedback_bridge = false;
    bool environment_and_barrier_bridge = false;
    bool load_and_action_bridge = false;
    bool is_single_connected_network = false;
    bool has_continuous_global_reaction = false;
    bool unification_circuit_online = false;
    std::array<SourceBodyIntegrationBridgeReport, kSourceBodyIntegrationBridgeCount>
        bridges;
    std::vector<SourceLawId> blocking_reason_ids;
    std::vector<SourceLawId> reaction_blocking_reason_ids;

    friend bool operator==(const SourceBodyIntegrationReport&,
                           const SourceBodyIntegrationReport&) = default;
};

// Evaluator output used by the spell compiler. It deliberately contains only
// values captured from an authoritative body and immutable content snapshot.
struct SourceLawBodyCapabilitySnapshot {
    uint64_t body_revision = 0;
    SourceLawId active_path_id;
    bool active_path_is_resonant = false;
    std::vector<SourceLawId> active_system_ids;
    std::vector<SourceLawId> available_intrinsic_ids;
    std::vector<SourceLawId> available_product_ids;
    SourceCircuitSchedule circuit_schedule;
    SourceBodyIntegrationReport integration;
    float source_throughput = 0.0F;
    float stability = 0.0F;

    friend bool operator==(const SourceLawBodyCapabilitySnapshot&,
                           const SourceLawBodyCapabilitySnapshot&) = default;
};

struct SourceLawSpellCompileReport {
    bool is_compilable = false;
    float available_effect_budget = 0.0F;
    std::optional<SourceLawId> primary_circuit_system_id;
    std::vector<SourceLawId> coordinating_circuit_system_ids;
    std::vector<SourceLawId> applied_path_core_ids;
    std::vector<SourceLawId> satisfied_hybrid_link_ids;
    float required_throughput = 0.0F;
    int32_t mana_cost = 0;
    std::vector<SourceLawId> risk_tags;
    std::vector<SourceLawId> blocking_reason_ids;

    friend bool operator==(const SourceLawSpellCompileReport&,
                           const SourceLawSpellCompileReport&) = default;
};

struct CompiledSourceLawSpell {
    SourceLawSpellProgramId program_id;
    uint32_t source_revision = 0;
    uint64_t body_revision = 0;
    SourceLawSpellCompileReport report;
    std::vector<SourceLawId> executable_operation_ids;

    friend bool operator==(const CompiledSourceLawSpell&, const CompiledSourceLawSpell&) = default;
};

struct SourceLawBodyState {
    SourceLawId active_path_id;
    SourceBodyStage stage = SourceBodyStage::kDormant;
    uint16_t source_level = 0;
    uint64_t body_revision = 0;

    int32_t source_reserve_current = 0;
    int32_t source_reserve_max = 0;
    float source_throughput = 0.0F;

    int32_t mana_current = 0;
    int32_t mana_max = 0;

    float stability = 100.0F;
    float mutation = 0.0F;
    int32_t mental_load = 0;

    std::array<std::optional<OrganInstance>, kSourceOrganSlotCount> organs;
    SourceCircuitSchedule circuit_schedule;
    SourceBodyIntegrationReport integration;

    friend bool operator==(const SourceLawBodyState&, const SourceLawBodyState&) = default;
};

struct PlayerSourceLawSpellProgram {
    SourceLawSpellProgramId program_id;
    std::optional<SourceLawId> copied_from_graph_id;
    std::string display_name;
    SourceLawSpellGraph graph;
    uint32_t source_revision = 0;

    friend bool operator==(const PlayerSourceLawSpellProgram&,
                           const PlayerSourceLawSpellProgram&) = default;
};

struct PlayerSourceLawState {
    SourceLawBodyState body;
    std::vector<SourceLawId> discovered_system_ids;
    std::vector<PlayerSourceLawSpellProgram> personal_spell_programs;

    friend bool operator==(const PlayerSourceLawState&, const PlayerSourceLawState&) = default;
};

struct CreatureSourceLawState {
    SourceLawBodyState body;
    SourceLawId creature_species_id;
    SourceLawId body_template_id;
    SourceLawId current_behavior_id;
    SourceLawId current_habitat_id;
    std::vector<SourceLawId> learned_or_evolved_spell_graph_ids;

    friend bool operator==(const CreatureSourceLawState&, const CreatureSourceLawState&) = default;
};

// Dynamic ecology and ritual facts are captured before worker evaluation.
// The evaluator never reaches back into World, ECS, a script VM, or UI.
struct SourceLawEvaluationContext {
    std::vector<SourceLawId> satisfied_ecology_tags;
    std::vector<SourceLawId> satisfied_ritual_ids;
    std::vector<SourceLawId> satisfied_bloodline_tags;
};

}  // namespace snt::game::source_law
