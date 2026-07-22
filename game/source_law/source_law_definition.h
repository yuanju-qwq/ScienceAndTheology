// Immutable content contracts for the source-law body evaluator.
//
// Definition ids are stable content strings. Runtime numeric ResourceKey
// values are deliberately excluded because a source-law snapshot must remain
// valid across worker evaluation and content reload boundaries.

#pragma once

#include "core/expected.h"
#include "game/source_law/source_law_types.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace snt::game::source_law {

struct ElementalPhysiologyRule {
    SourceLawElement element = SourceLawElement::kCount;
    std::vector<ElementalPhysiologyAction> allowed_actions;
    std::vector<ElementalReactionStage> allowed_stages;
    std::vector<ElementalPhysiologyAction> allowed_next_actions;
    std::vector<ElementalPhysiologyAction> competing_actions;
    std::vector<SourceLawId> required_buffer_tags;
};

struct OrganElementalContribution {
    SourceLawElement element = SourceLawElement::kCount;
    ElementalPhysiologyAction action = ElementalPhysiologyAction::kCount;
    ElementalReactionStage stage = ElementalReactionStage::kCount;
    float base_capacity = 0.0F;
    bool is_primary_for_stage = false;
    std::vector<SourceLawId> byproduct_tags;
};

struct OrganDefinition {
    SourceLawId id;
    SourceOrganSlot slot = SourceOrganSlot::kCount;
    std::vector<OrganSystemRole> roles;
    std::vector<OrganElementalContribution> elemental_contributions;
    SourceLawId purity_profile_id;

    std::vector<SourceLawId> bloodline_tags;
    std::vector<SourceLawId> ecology_tags;
    std::vector<SourceLawId> system_tags;
    std::vector<SourceLawId> native_path_tags;

    float base_stability_modifier = 0.0F;
    float base_mutation_risk = 0.0F;
    int32_t base_mental_load = 0;
    std::vector<SourceLawId> pressure_tags;
};

struct OrganSystemRequirement {
    std::vector<SourceOrganSlot> allowed_slots;
    std::vector<OrganSystemRole> required_roles;
    std::vector<SourceLawId> required_tags;
    uint8_t minimum_matches = 1;
};

struct ElementalReactionStepDefinition {
    SourceLawId id;
    ElementalReactionStage stage = ElementalReactionStage::kCount;
    std::vector<ElementalPhysiologyAction> allowed_actions;
    std::vector<SourceLawElement> allowed_elements;
    uint8_t minimum_contributors = 1;
    bool requires_distinct_organ_from_previous_step = true;
};

struct ElementalReactionDefinition {
    SourceLawId id;
    std::vector<ElementalReactionStepDefinition> closure_steps;
    std::vector<ElementalReactionStepDefinition> growth_steps;
    SourceLawId product_definition_id;
    std::vector<SourceLawId> byproduct_tags;
    std::vector<SourceLawId> required_relief_tags;
};

struct OrganSystemDefinition {
    SourceLawId id;
    SourceBodySystem body_system = SourceBodySystem::kCount;
    std::vector<OrganSystemRequirement> resonance_requirements;
    std::vector<OrganSystemRequirement> closure_requirements;
    std::vector<OrganSystemRequirement> growth_link_requirements;
    SourceLawId elemental_reaction_id;

    std::vector<SourceLawId> allowed_bloodline_relations;
    std::vector<SourceLawId> ecology_conditions;
    SourceLawId base_active_ability_id;
    std::vector<SourceLawId> pressure_tags;
};

struct SourceBodyIntegrationBridgeDefinition {
    SourceBodyIntegrationBridge bridge = SourceBodyIntegrationBridge::kCount;
    SourceLawId id;
    std::vector<SourceOrganSlot> required_slots;
    std::vector<OrganSystemRole> required_roles;
    std::vector<SourceLawId> required_tags;
    std::vector<ElementalReactionStage> required_reaction_stages;
    std::vector<ElementalPhysiologyAction> required_actions;
};

struct SourceBodyIntegrationDefinition {
    SourceLawId id;
    std::array<SourceBodyIntegrationBridgeDefinition,
               kSourceBodyIntegrationBridgeCount>
        required_bridges;
    float minimum_stability = 0.0F;
    float maximum_mutation = 100.0F;
    SourceLawId integration_ritual_id;
    std::vector<SourceLawId> ecology_conditions;
};

struct SourcePathReactionPreference {
    SourceBodySystem body_system = SourceBodySystem::kCount;
    ElementalReactionStage stage = ElementalReactionStage::kCount;
    ElementalPhysiologyAction action = ElementalPhysiologyAction::kCount;
    SourceLawId product_modifier_id;
    SourceLawId byproduct_handling_modifier_id;
};

struct SourcePathDefinition {
    SourceLawId id;
    std::vector<SourceBodySystem> preferred_systems;
    std::vector<SourcePathReactionPreference> reaction_preferences;
    std::vector<SourceLawId> core_organ_tags;
    std::vector<SourceLawId> resonance_rules;
    SourceLawId signature_ability_id;
    SourceLawId completion_body_ability_id;
    SourceLawId domain_ability_id;
    std::vector<SourceLawId> severe_conflict_tags;
};

struct BloodlineProfile {
    SourceLawId id;
    std::vector<SourceLawId> lineage_tags;
    std::vector<SourceLawId> ecology_tags;
    std::vector<SourceLawId> innate_reaction_ids;
    std::vector<SourceLawId> compatible_bloodline_tags;
    std::vector<SourceLawId> hostile_bloodline_tags;
    std::vector<SourceLawId> symbiosis_tags;
};

struct SourceBodyTemplate {
    SourceLawId id;
    SourceLawId innate_path_id;
    std::array<std::vector<SourceLawId>, kSourceOrganSlotCount> organ_candidates;
    std::vector<SourceLawId> initial_system_ids;
    std::vector<SourceLawId> initial_reaction_ids;
    std::vector<SourceLawId> integration_condition_ids;
};

struct CreatureSourceBodyDefinition {
    SourceLawId creature_species_id;
    SourceLawId bloodline_profile_id;
    SourceLawId body_template_id;
    SourceLawId behavior_profile_id;
    std::vector<SourceLawId> sample_definition_ids;
    std::vector<SourceLawId> ecology_conditions;
};

// The snapshot is immutable once it crosses into simulation work. All maps
// are ordered so evaluation diagnostics remain deterministic across runs.
class SourceLawContentSnapshot final {
public:
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const ElementalPhysiologyRule* find_element_rule(
        SourceLawElement element) const noexcept;
    [[nodiscard]] const OrganDefinition* find_organ(const SourceLawId& id) const;
    [[nodiscard]] const ElementalReactionDefinition* find_reaction(
        const SourceLawId& id) const;
    [[nodiscard]] const OrganSystemDefinition* find_system(const SourceLawId& id) const;
    [[nodiscard]] const SourcePathDefinition* find_path(const SourceLawId& id) const;
    [[nodiscard]] const BloodlineProfile* find_bloodline(const SourceLawId& id) const;
    [[nodiscard]] const SourceBodyTemplate* find_body_template(const SourceLawId& id) const;
    [[nodiscard]] const CreatureSourceBodyDefinition* find_creature_body(
        const SourceLawId& creature_species_id) const;
    [[nodiscard]] const SourceBodyIntegrationDefinition* integration_definition() const noexcept {
        return integration_definition_ ? &*integration_definition_ : nullptr;
    }
    [[nodiscard]] const std::map<SourceLawId, OrganSystemDefinition>& systems() const noexcept {
        return systems_;
    }

private:
    friend class SourceLawContentBuilder;

    uint64_t revision_ = 0;
    std::array<std::optional<ElementalPhysiologyRule>, kSourceLawElementCount>
        element_rules_;
    std::map<SourceLawId, OrganDefinition> organs_;
    std::map<SourceLawId, ElementalReactionDefinition> reactions_;
    std::map<SourceLawId, OrganSystemDefinition> systems_;
    std::map<SourceLawId, SourcePathDefinition> paths_;
    std::map<SourceLawId, BloodlineProfile> bloodlines_;
    std::map<SourceLawId, SourceBodyTemplate> body_templates_;
    std::map<SourceLawId, CreatureSourceBodyDefinition> creature_bodies_;
    std::optional<SourceBodyIntegrationDefinition> integration_definition_;
};

// Mutable only during content loading. build() validates every cross-reference
// and publishes a value snapshot with no script, World, UI, or network handle.
class SourceLawContentBuilder final {
public:
    [[nodiscard]] snt::core::Expected<void> add_element_rule(ElementalPhysiologyRule definition);
    [[nodiscard]] snt::core::Expected<void> add_organ(OrganDefinition definition);
    [[nodiscard]] snt::core::Expected<void> add_reaction(ElementalReactionDefinition definition);
    [[nodiscard]] snt::core::Expected<void> add_system(OrganSystemDefinition definition);
    [[nodiscard]] snt::core::Expected<void> add_path(SourcePathDefinition definition);
    [[nodiscard]] snt::core::Expected<void> add_bloodline(BloodlineProfile definition);
    [[nodiscard]] snt::core::Expected<void> add_body_template(SourceBodyTemplate definition);
    [[nodiscard]] snt::core::Expected<void> add_creature_body(
        CreatureSourceBodyDefinition definition);
    [[nodiscard]] snt::core::Expected<void> set_integration_definition(
        SourceBodyIntegrationDefinition definition);

    [[nodiscard]] snt::core::Expected<SourceLawContentSnapshot> build(
        uint64_t revision) &&;

private:
    [[nodiscard]] static snt::core::Expected<void> validate_snapshot(
        const SourceLawContentSnapshot& snapshot);

    SourceLawContentSnapshot snapshot_;
};

}  // namespace snt::game::source_law
