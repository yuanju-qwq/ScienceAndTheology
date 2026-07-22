// Source-law content snapshot loading and validation.

#define SNT_LOG_CHANNEL "game.source_law.content"
#include "game/source_law/source_law_definition.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>

namespace snt::game::source_law {
namespace {

constexpr size_t kMaxSourceLawIdBytes = 192;
constexpr uint8_t kMaxRequirementMatches = 8;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] bool is_valid_id(const SourceLawId& id) noexcept {
    return !id.empty() && id.size() <= kMaxSourceLawIdBytes &&
           id.find('\0') == SourceLawId::npos;
}

template <typename T, typename IsValid>
[[nodiscard]] bool is_unique_and_valid(const std::vector<T>& values, IsValid&& is_valid) {
    std::set<uint8_t> seen;
    for (const T value : values) {
        if (!is_valid(value) || !seen.insert(static_cast<uint8_t>(value)).second) return false;
    }
    return true;
}

[[nodiscard]] bool has_unique_valid_ids(const std::vector<SourceLawId>& values) {
    std::set<SourceLawId> seen;
    for (const SourceLawId& value : values) {
        if (!is_valid_id(value) || !seen.insert(value).second) return false;
    }
    return true;
}

template <typename T>
[[nodiscard]] bool contains(const std::vector<T>& values, T target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

[[nodiscard]] bool rule_allows(const ElementalPhysiologyRule& rule,
                                ElementalPhysiologyAction action,
                                ElementalReactionStage stage) {
    return contains(rule.allowed_actions, action) && contains(rule.allowed_stages, stage);
}

[[nodiscard]] snt::core::Expected<void> validate_rule(
    const ElementalPhysiologyRule& definition) {
    if (!is_valid_source_law_element(definition.element) || definition.allowed_actions.empty() ||
        definition.allowed_stages.empty() ||
        !is_unique_and_valid(definition.allowed_actions,
                             is_valid_elemental_physiology_action) ||
        !is_unique_and_valid(definition.allowed_stages,
                             is_valid_elemental_reaction_stage) ||
        !is_unique_and_valid(definition.allowed_next_actions,
                             is_valid_elemental_physiology_action) ||
        !is_unique_and_valid(definition.competing_actions,
                             is_valid_elemental_physiology_action) ||
        !has_unique_valid_ids(definition.required_buffer_tags)) {
        return invalid_argument("Source-law elemental physiology rule is invalid");
    }
    for (const ElementalPhysiologyAction action : definition.allowed_next_actions) {
        if (contains(definition.competing_actions, action)) {
            return invalid_argument("Source-law elemental physiology rule declares one action as both next and competing");
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_requirement(
    const OrganSystemRequirement& requirement) {
    if (requirement.minimum_matches == 0 ||
        requirement.minimum_matches > kMaxRequirementMatches ||
        !is_unique_and_valid(requirement.allowed_slots, is_valid_source_organ_slot) ||
        !is_unique_and_valid(requirement.required_roles, is_valid_organ_system_role) ||
        !has_unique_valid_ids(requirement.required_tags)) {
        return invalid_argument("Source-law system requirement is invalid");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_step(
    const ElementalReactionStepDefinition& step) {
    if (!is_valid_id(step.id) || !is_valid_elemental_reaction_stage(step.stage) ||
        step.minimum_contributors == 0 || step.minimum_contributors > kMaxRequirementMatches ||
        step.allowed_actions.empty() || step.allowed_elements.empty() ||
        !is_unique_and_valid(step.allowed_actions,
                             is_valid_elemental_physiology_action) ||
        !is_unique_and_valid(step.allowed_elements, is_valid_source_law_element)) {
        return invalid_argument("Source-law elemental reaction step is invalid: " + step.id);
    }
    return {};
}

[[nodiscard]] bool covers_required_closure_stages(
    const std::vector<ElementalReactionStepDefinition>& steps) {
    bool has_source = false;
    bool has_transport_or_transform = false;
    bool has_effect = false;
    for (const ElementalReactionStepDefinition& step : steps) {
        has_source = has_source || step.stage == ElementalReactionStage::kGenerationOrIntake;
        has_transport_or_transform = has_transport_or_transform ||
            step.stage == ElementalReactionStage::kTransport ||
            step.stage == ElementalReactionStage::kTransformation;
        has_effect = has_effect || step.stage == ElementalReactionStage::kEffectOrRelease;
    }
    return has_source && has_transport_or_transform && has_effect;
}

[[nodiscard]] bool requirement_set_declares_role(
    const std::vector<OrganSystemRequirement>& requirements, OrganSystemRole role) {
    return std::any_of(requirements.begin(), requirements.end(), [role](const auto& requirement) {
        return contains(requirement.required_roles, role);
    });
}

[[nodiscard]] bool template_can_supply_step(const SourceBodyTemplate& body_template,
                                             const ElementalReactionStepDefinition& step,
                                             const SourceLawContentSnapshot& snapshot) {
    for (size_t slot_index = 0; slot_index < kSourceOrganSlotCount; ++slot_index) {
        for (const SourceLawId& organ_id : body_template.organ_candidates[slot_index]) {
            const OrganDefinition* organ = snapshot.find_organ(organ_id);
            if (organ == nullptr) continue;
            for (const OrganElementalContribution& contribution : organ->elemental_contributions) {
                if (contribution.stage == step.stage &&
                    contains(step.allowed_actions, contribution.action) &&
                    contains(step.allowed_elements, contribution.element)) {
                    return true;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] bool path_preference_has_declared_action(
    const SourcePathReactionPreference& preference,
    const SourceLawContentSnapshot& snapshot) {
    for (const auto& [system_id, system] : snapshot.systems()) {
        static_cast<void>(system_id);
        if (system.body_system != preference.body_system) continue;
        const ElementalReactionDefinition* reaction = snapshot.find_reaction(
            system.elemental_reaction_id);
        if (reaction == nullptr) continue;
        const auto has_step = [&preference](const auto& steps) {
            return std::any_of(steps.begin(), steps.end(), [&preference](const auto& step) {
                return step.stage == preference.stage &&
                       contains(step.allowed_actions, preference.action);
            });
        };
        if (has_step(reaction->closure_steps) || has_step(reaction->growth_steps)) return true;
    }
    return false;
}

}  // namespace

snt::core::Expected<void> SourceLawContentBuilder::validate_snapshot(
    const SourceLawContentSnapshot& snapshot) {
    if (snapshot.revision_ == 0) {
        return invalid_argument("Source-law content revision must be non-zero");
    }
    for (size_t index = 0; index < kSourceLawElementCount; ++index) {
        if (!snapshot.element_rules_[index] ||
            snapshot.element_rules_[index]->element != static_cast<SourceLawElement>(index)) {
            return invalid_argument("Source-law content must define exactly one physiology rule for every element");
        }
        if (auto result = validate_rule(*snapshot.element_rules_[index]); !result) {
            return result.error();
        }
    }

    for (const auto& [id, organ] : snapshot.organs_) {
        if (id != organ.id || !is_valid_id(id) || !is_valid_source_organ_slot(organ.slot) ||
            organ.roles.empty() || organ.elemental_contributions.empty() ||
            !is_unique_and_valid(organ.roles, is_valid_organ_system_role) ||
            !has_unique_valid_ids(organ.bloodline_tags) ||
            !has_unique_valid_ids(organ.ecology_tags) ||
            !has_unique_valid_ids(organ.system_tags) ||
            !has_unique_valid_ids(organ.native_path_tags) ||
            !has_unique_valid_ids(organ.pressure_tags) ||
            !std::isfinite(organ.base_stability_modifier) ||
            !std::isfinite(organ.base_mutation_risk)) {
            return invalid_argument("Source-law organ definition is invalid: " + id);
        }
        for (const OrganElementalContribution& contribution : organ.elemental_contributions) {
            const ElementalPhysiologyRule* rule = snapshot.find_element_rule(
                contribution.element);
            if (rule == nullptr || !is_valid_elemental_physiology_action(contribution.action) ||
                !is_valid_elemental_reaction_stage(contribution.stage) ||
                !std::isfinite(contribution.base_capacity) || contribution.base_capacity <= 0.0F ||
                !has_unique_valid_ids(contribution.byproduct_tags) ||
                !rule_allows(*rule, contribution.action, contribution.stage)) {
                return invalid_argument("Source-law organ contribution violates physiology rule: " + id);
            }
        }
    }

    for (const auto& [id, reaction] : snapshot.reactions_) {
        if (id != reaction.id || !is_valid_id(id) || reaction.closure_steps.empty() ||
            !is_valid_id(reaction.product_definition_id) ||
            !has_unique_valid_ids(reaction.byproduct_tags) ||
            !has_unique_valid_ids(reaction.required_relief_tags) ||
            (!reaction.byproduct_tags.empty() && reaction.required_relief_tags.empty()) ||
            !covers_required_closure_stages(reaction.closure_steps)) {
            return invalid_argument("Source-law reaction definition is invalid: " + id);
        }
        std::set<SourceLawId> step_ids;
        for (const ElementalReactionStepDefinition& step : reaction.closure_steps) {
            if (auto result = validate_step(step); !result) return result.error();
            if (!step_ids.insert(step.id).second) {
                return invalid_argument("Source-law reaction has duplicate closure step: " + id);
            }
        }
        for (const ElementalReactionStepDefinition& step : reaction.growth_steps) {
            if (auto result = validate_step(step); !result) return result.error();
            if (!step_ids.insert(step.id).second) {
                return invalid_argument("Source-law reaction has duplicate growth step: " + id);
            }
        }
    }

    for (const auto& [id, system] : snapshot.systems_) {
        if (id != system.id || !is_valid_id(id) ||
            !is_valid_source_body_system(system.body_system) ||
            system.resonance_requirements.empty() || system.closure_requirements.empty() ||
            snapshot.find_reaction(system.elemental_reaction_id) == nullptr ||
            !has_unique_valid_ids(system.allowed_bloodline_relations) ||
            !has_unique_valid_ids(system.ecology_conditions) ||
            !has_unique_valid_ids(system.pressure_tags)) {
            return invalid_argument("Source-law system definition is invalid: " + id);
        }
        for (const OrganSystemRequirement& requirement : system.resonance_requirements) {
            if (auto result = validate_requirement(requirement); !result) return result.error();
        }
        for (const OrganSystemRequirement& requirement : system.closure_requirements) {
            if (auto result = validate_requirement(requirement); !result) return result.error();
        }
        for (const OrganSystemRequirement& requirement : system.growth_link_requirements) {
            if (auto result = validate_requirement(requirement); !result) return result.error();
        }
        std::vector<OrganSystemRequirement> all_requirements = system.resonance_requirements;
        all_requirements.insert(all_requirements.end(), system.closure_requirements.begin(),
                                system.closure_requirements.end());
        if (!requirement_set_declares_role(all_requirements, OrganSystemRole::kCore) ||
            !requirement_set_declares_role(all_requirements, OrganSystemRole::kConduit) ||
            !requirement_set_declares_role(all_requirements, OrganSystemRole::kEffector)) {
            return invalid_argument("Source-law system must declare core, conduit, and effector requirements: " + id);
        }
    }

    if (!snapshot.integration_definition_) {
        return invalid_argument("Source-law content is missing the body integration definition");
    }
    const SourceBodyIntegrationDefinition& integration = *snapshot.integration_definition_;
    if (!is_valid_id(integration.id) || !std::isfinite(integration.minimum_stability) ||
        !std::isfinite(integration.maximum_mutation) || integration.minimum_stability < 0.0F ||
        integration.minimum_stability > 100.0F || integration.maximum_mutation < 0.0F ||
        integration.maximum_mutation > 100.0F ||
        !is_valid_id(integration.integration_ritual_id) ||
        !has_unique_valid_ids(integration.ecology_conditions)) {
        return invalid_argument("Source-law body integration definition is invalid");
    }
    std::array<bool, kSourceOrganSlotCount> integration_slots{};
    for (size_t index = 0; index < kSourceBodyIntegrationBridgeCount; ++index) {
        const SourceBodyIntegrationBridgeDefinition& bridge = integration.required_bridges[index];
        if (bridge.bridge != static_cast<SourceBodyIntegrationBridge>(index) ||
            !is_valid_id(bridge.id) || bridge.required_slots.empty() ||
            bridge.required_roles.empty() || bridge.required_reaction_stages.empty() ||
            bridge.required_actions.empty() ||
            !is_unique_and_valid(bridge.required_slots, is_valid_source_organ_slot) ||
            !is_unique_and_valid(bridge.required_roles, is_valid_organ_system_role) ||
            !is_unique_and_valid(bridge.required_reaction_stages,
                                 is_valid_elemental_reaction_stage) ||
            !is_unique_and_valid(bridge.required_actions,
                                 is_valid_elemental_physiology_action) ||
            !has_unique_valid_ids(bridge.required_tags)) {
            return invalid_argument("Source-law integration bridge is invalid");
        }
        for (const SourceOrganSlot slot : bridge.required_slots) {
            integration_slots[static_cast<size_t>(slot)] = true;
        }
    }
    if (std::any_of(integration_slots.begin(), integration_slots.end(),
                    [](bool covered) { return !covered; })) {
        return invalid_argument("Source-law integration bridges must cover all eight organ slots");
    }

    for (const auto& [id, path] : snapshot.paths_) {
        if (id != path.id || !is_valid_id(id) || path.preferred_systems.empty() ||
            !is_unique_and_valid(path.preferred_systems, is_valid_source_body_system) ||
            !has_unique_valid_ids(path.core_organ_tags) ||
            !has_unique_valid_ids(path.resonance_rules) ||
            !has_unique_valid_ids(path.severe_conflict_tags)) {
            return invalid_argument("Source-law path definition is invalid: " + id);
        }
        for (const SourcePathReactionPreference& preference : path.reaction_preferences) {
            if (!is_valid_source_body_system(preference.body_system) ||
                !is_valid_elemental_reaction_stage(preference.stage) ||
                !is_valid_elemental_physiology_action(preference.action) ||
                !path_preference_has_declared_action(preference, snapshot)) {
                return invalid_argument("Source-law path preference modifies a missing reaction action: " + id);
            }
        }
    }

    for (const auto& [id, bloodline] : snapshot.bloodlines_) {
        if (id != bloodline.id || !is_valid_id(id) ||
            !has_unique_valid_ids(bloodline.lineage_tags) ||
            !has_unique_valid_ids(bloodline.ecology_tags) ||
            !has_unique_valid_ids(bloodline.innate_reaction_ids) ||
            !has_unique_valid_ids(bloodline.compatible_bloodline_tags) ||
            !has_unique_valid_ids(bloodline.hostile_bloodline_tags) ||
            !has_unique_valid_ids(bloodline.symbiosis_tags)) {
            return invalid_argument("Source-law bloodline profile is invalid: " + id);
        }
        for (const SourceLawId& reaction_id : bloodline.innate_reaction_ids) {
            if (snapshot.find_reaction(reaction_id) == nullptr) {
                return invalid_argument("Source-law bloodline references a missing reaction: " + id);
            }
        }
    }

    for (const auto& [id, body_template] : snapshot.body_templates_) {
        if (id != body_template.id || !is_valid_id(id) ||
            (!body_template.innate_path_id.empty() &&
             snapshot.find_path(body_template.innate_path_id) == nullptr) ||
            !has_unique_valid_ids(body_template.initial_system_ids) ||
            !has_unique_valid_ids(body_template.initial_reaction_ids) ||
            !has_unique_valid_ids(body_template.integration_condition_ids)) {
            return invalid_argument("Source-law body template is invalid: " + id);
        }
        for (size_t slot_index = 0; slot_index < kSourceOrganSlotCount; ++slot_index) {
            const auto& candidates = body_template.organ_candidates[slot_index];
            if (candidates.empty() || !has_unique_valid_ids(candidates)) {
                return invalid_argument("Source-law body template has an unmapped organ slot: " + id);
            }
            for (const SourceLawId& organ_id : candidates) {
                const OrganDefinition* organ = snapshot.find_organ(organ_id);
                if (organ == nullptr || organ->slot != static_cast<SourceOrganSlot>(slot_index)) {
                    return invalid_argument("Source-law body template organ candidate mismatches its slot: " + id);
                }
            }
        }
        for (const SourceLawId& system_id : body_template.initial_system_ids) {
            if (snapshot.find_system(system_id) == nullptr) {
                return invalid_argument("Source-law body template references a missing system: " + id);
            }
        }
        for (const SourceLawId& reaction_id : body_template.initial_reaction_ids) {
            const ElementalReactionDefinition* reaction = snapshot.find_reaction(reaction_id);
            if (reaction == nullptr) {
                return invalid_argument("Source-law body template references a missing reaction: " + id);
            }
            for (const ElementalReactionStepDefinition& step : reaction->closure_steps) {
                if (!template_can_supply_step(body_template, step, snapshot)) {
                    return invalid_argument("Source-law body template cannot supply an initial reaction step: " + id);
                }
            }
        }
    }

    for (const auto& [species_id, creature] : snapshot.creature_bodies_) {
        if (species_id != creature.creature_species_id || !is_valid_id(species_id) ||
            snapshot.find_bloodline(creature.bloodline_profile_id) == nullptr ||
            snapshot.find_body_template(creature.body_template_id) == nullptr ||
            !is_valid_id(creature.behavior_profile_id) ||
            !has_unique_valid_ids(creature.sample_definition_ids) ||
            !has_unique_valid_ids(creature.ecology_conditions)) {
            return invalid_argument("Source-law creature body definition is invalid: " + species_id);
        }
    }
    return {};
}

namespace {

template <typename T>
[[nodiscard]] const T* find_or_null(const std::map<SourceLawId, T>& definitions,
                                    const SourceLawId& id) {
    const auto found = definitions.find(id);
    return found == definitions.end() ? nullptr : &found->second;
}

template <typename T>
[[nodiscard]] snt::core::Expected<void> add_unique_definition(
    std::map<SourceLawId, T>& definitions, T definition, const char* kind) {
    if (!is_valid_id(definition.id)) {
        return invalid_argument(std::string{"Source-law "} + kind + " id is invalid");
    }
    const SourceLawId id = definition.id;
    if (!definitions.emplace(id, std::move(definition)).second) {
        return invalid_argument(std::string{"Duplicate source-law "} + kind + " id: " + id);
    }
    return {};
}

}  // namespace

const ElementalPhysiologyRule* SourceLawContentSnapshot::find_element_rule(
    SourceLawElement element) const noexcept {
    if (!is_valid_source_law_element(element)) return nullptr;
    const std::optional<ElementalPhysiologyRule>& rule =
        element_rules_[static_cast<size_t>(element)];
    return rule ? &*rule : nullptr;
}

const OrganDefinition* SourceLawContentSnapshot::find_organ(const SourceLawId& id) const {
    return find_or_null(organs_, id);
}

const ElementalReactionDefinition* SourceLawContentSnapshot::find_reaction(
    const SourceLawId& id) const {
    return find_or_null(reactions_, id);
}

const OrganSystemDefinition* SourceLawContentSnapshot::find_system(const SourceLawId& id) const {
    return find_or_null(systems_, id);
}

const SourcePathDefinition* SourceLawContentSnapshot::find_path(const SourceLawId& id) const {
    return find_or_null(paths_, id);
}

const BloodlineProfile* SourceLawContentSnapshot::find_bloodline(const SourceLawId& id) const {
    return find_or_null(bloodlines_, id);
}

const SourceBodyTemplate* SourceLawContentSnapshot::find_body_template(
    const SourceLawId& id) const {
    return find_or_null(body_templates_, id);
}

const CreatureSourceBodyDefinition* SourceLawContentSnapshot::find_creature_body(
    const SourceLawId& creature_species_id) const {
    return find_or_null(creature_bodies_, creature_species_id);
}

snt::core::Expected<void> SourceLawContentBuilder::add_element_rule(
    ElementalPhysiologyRule definition) {
    if (!is_valid_source_law_element(definition.element)) {
        return invalid_argument("Source-law elemental physiology rule has an invalid element");
    }
    const size_t index = static_cast<size_t>(definition.element);
    if (snapshot_.element_rules_[index]) {
        return invalid_argument("Duplicate source-law elemental physiology rule");
    }
    snapshot_.element_rules_[index] = std::move(definition);
    return {};
}

snt::core::Expected<void> SourceLawContentBuilder::add_organ(OrganDefinition definition) {
    return add_unique_definition(snapshot_.organs_, std::move(definition), "organ");
}

snt::core::Expected<void> SourceLawContentBuilder::add_reaction(
    ElementalReactionDefinition definition) {
    return add_unique_definition(snapshot_.reactions_, std::move(definition), "reaction");
}

snt::core::Expected<void> SourceLawContentBuilder::add_system(
    OrganSystemDefinition definition) {
    return add_unique_definition(snapshot_.systems_, std::move(definition), "system");
}

snt::core::Expected<void> SourceLawContentBuilder::add_path(SourcePathDefinition definition) {
    return add_unique_definition(snapshot_.paths_, std::move(definition), "path");
}

snt::core::Expected<void> SourceLawContentBuilder::add_bloodline(BloodlineProfile definition) {
    return add_unique_definition(snapshot_.bloodlines_, std::move(definition), "bloodline");
}

snt::core::Expected<void> SourceLawContentBuilder::add_body_template(
    SourceBodyTemplate definition) {
    return add_unique_definition(snapshot_.body_templates_, std::move(definition), "body template");
}

snt::core::Expected<void> SourceLawContentBuilder::add_creature_body(
    CreatureSourceBodyDefinition definition) {
    if (!is_valid_id(definition.creature_species_id)) {
        return invalid_argument("Source-law creature body species id is invalid");
    }
    const SourceLawId id = definition.creature_species_id;
    if (!snapshot_.creature_bodies_.emplace(id, std::move(definition)).second) {
        return invalid_argument("Duplicate source-law creature body species id: " + id);
    }
    return {};
}

snt::core::Expected<void> SourceLawContentBuilder::set_integration_definition(
    SourceBodyIntegrationDefinition definition) {
    if (snapshot_.integration_definition_) {
        return invalid_argument("Source-law integration definition was registered more than once");
    }
    snapshot_.integration_definition_ = std::move(definition);
    return {};
}

snt::core::Expected<SourceLawContentSnapshot> SourceLawContentBuilder::build(
    uint64_t revision) && {
    snapshot_.revision_ = revision;
    if (auto result = validate_snapshot(snapshot_); !result) return result.error();
    SNT_LOG_INFO("Published source-law content snapshot revision=%llu organs=%zu systems=%zu paths=%zu creatures=%zu",
                 static_cast<unsigned long long>(snapshot_.revision_), snapshot_.organs_.size(),
                 snapshot_.systems_.size(), snapshot_.paths_.size(),
                 snapshot_.creature_bodies_.size());
    return std::move(snapshot_);
}

}  // namespace snt::game::source_law
