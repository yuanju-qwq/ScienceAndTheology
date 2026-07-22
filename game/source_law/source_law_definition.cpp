// Source-law content snapshot loading and validation.

#define SNT_LOG_CHANNEL "game.source_law.content"
#include "game/source_law/source_law_definition.h"
#include "game/source_law/source_law_spell_compiler.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace snt::game::source_law {
namespace {

constexpr size_t kMaxSourceLawIdBytes = 192;
constexpr uint8_t kMaxRequirementMatches = 8;
constexpr size_t kMaxSpellGraphNodes = 128;
constexpr size_t kMaxSpellGraphLinks = 512;
constexpr size_t kMaxSpellNodeParameters = 32;
constexpr size_t kMaxSpellGraphDeclaredSystems = 8;
constexpr uint16_t kMaxSpellControlSteps = 256;

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

[[nodiscard]] snt::core::Expected<void> validate_intrinsic(
    const SourceLawIntrinsicDefinition& definition) {
    if (!is_valid_id(definition.id) || definition.required_closed_systems.empty() ||
        definition.output_port_types.empty() ||
        !is_unique_and_valid(definition.required_closed_systems,
                             is_valid_source_body_system) ||
        !is_unique_and_valid(definition.required_stages,
                             is_valid_elemental_reaction_stage) ||
        !is_unique_and_valid(definition.required_actions,
                             is_valid_elemental_physiology_action) ||
        !has_unique_valid_ids(definition.required_product_tags) ||
        !is_unique_and_valid(definition.input_port_types,
                             is_valid_source_law_spell_port_type) ||
        !is_unique_and_valid(definition.output_port_types,
                             is_valid_source_law_spell_port_type) ||
        !has_unique_valid_ids(definition.emitted_byproduct_tags) ||
        !has_unique_valid_ids(definition.risk_tags) ||
        !std::isfinite(definition.required_throughput) ||
        definition.required_throughput < 0.0F || definition.mana_cost < 0) {
        return invalid_argument("Source-law intrinsic definition is invalid: " + definition.id);
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_hybrid_link(
    const SourceLawHybridLinkDefinition& definition) {
    if (!is_valid_id(definition.id) || definition.minimum_distinct_systems < 2 ||
        definition.minimum_distinct_systems > kSourceBodySystemCount ||
        definition.required_distinct_systems.size() < definition.minimum_distinct_systems ||
        definition.required_intrinsic_ids.size() < 2 || definition.required_product_ids.empty() ||
        !is_valid_id(definition.composite_semantic_id) ||
        !is_unique_and_valid(definition.required_distinct_systems,
                             is_valid_source_body_system) ||
        !has_unique_valid_ids(definition.required_intrinsic_ids) ||
        !has_unique_valid_ids(definition.required_product_ids) ||
        !has_unique_valid_ids(definition.emitted_byproduct_tags)) {
        return invalid_argument("Source-law hybrid link definition is invalid: " + definition.id);
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_spell_port(
    const SourceLawSpellPortDefinition& port) {
    if (!is_valid_id(port.id) || !is_valid_source_law_spell_port_type(port.type)) {
        return invalid_argument("Source-law spell port definition is invalid: " + port.id);
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_spell_node_definition(
    const SourceLawSpellNodeDefinition& definition) {
    if (!is_valid_id(definition.id) ||
        !is_valid_source_law_spell_node_kind(definition.kind) ||
        definition.kind == SourceLawSpellNodeKind::kBodyIntrinsic ||
        !has_unique_valid_ids(definition.emitted_byproduct_tags) ||
        !has_unique_valid_ids(definition.handled_byproduct_tags) ||
        !has_unique_valid_ids(definition.risk_tags) ||
        !std::isfinite(definition.required_throughput) ||
        definition.required_throughput < 0.0F || definition.mana_cost < 0 ||
        definition.maximum_control_steps > kMaxSpellControlSteps) {
        return invalid_argument("Source-law spell node definition is invalid: " + definition.id);
    }

    std::set<SourceLawId> port_ids;
    const auto validate_ports = [&port_ids](const auto& ports) -> snt::core::Expected<void> {
        for (const SourceLawSpellPortDefinition& port : ports) {
            if (auto result = validate_spell_port(port); !result) return result.error();
            if (!port_ids.insert(port.id).second) {
                return invalid_argument("Source-law spell node definition has duplicate port ids");
            }
        }
        return {};
    };
    if (auto result = validate_ports(definition.input_ports); !result) return result.error();
    if (auto result = validate_ports(definition.output_ports); !result) return result.error();

    switch (definition.kind) {
    case SourceLawSpellNodeKind::kInput:
        if (!definition.input_ports.empty() || definition.output_ports.empty() ||
            definition.maximum_control_steps != 0) {
            return invalid_argument("Source-law input node definition has an invalid port shape");
        }
        break;
    case SourceLawSpellNodeKind::kPathCore:
        if (definition.input_ports.empty() || definition.output_ports.empty() ||
            !is_valid_id(definition.semantic_id) || definition.maximum_control_steps != 0) {
            return invalid_argument("Source-law path core node definition is invalid");
        }
        break;
    case SourceLawSpellNodeKind::kControlFlow:
        if (definition.input_ports.empty() || definition.output_ports.empty() ||
            definition.maximum_control_steps == 0) {
            return invalid_argument("Source-law control-flow node requires a finite step bound");
        }
        break;
    case SourceLawSpellNodeKind::kCoordinatingService:
        if (definition.input_ports.empty() && definition.output_ports.empty() ||
            definition.maximum_control_steps != 0) {
            return invalid_argument("Source-law coordinating-service node definition is invalid");
        }
        break;
    case SourceLawSpellNodeKind::kOutput:
        if (definition.input_ports.empty() || definition.maximum_control_steps != 0) {
            return invalid_argument("Source-law output node definition has an invalid port shape");
        }
        break;
    case SourceLawSpellNodeKind::kBodyIntrinsic:
    case SourceLawSpellNodeKind::kCount:
        return invalid_argument("Source-law spell node definition has an unsupported kind");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_spell_graph_shape(
    const SourceLawSpellGraph& graph) {
    if (!is_valid_source_law_spell_graph_kind(graph.kind) || graph.nodes.empty() ||
        graph.nodes.size() > kMaxSpellGraphNodes || graph.links.size() > kMaxSpellGraphLinks ||
        graph.declared_max_control_steps > kMaxSpellControlSteps ||
        !has_unique_valid_ids(graph.required_path_core_ids) ||
        !has_unique_valid_ids(graph.requested_hybrid_link_ids) ||
        graph.declared_primary_system_ids.size() > kMaxSpellGraphDeclaredSystems ||
        graph.declared_coordinating_system_ids.size() > kMaxSpellGraphDeclaredSystems ||
        !has_unique_valid_ids(graph.declared_primary_system_ids) ||
        !has_unique_valid_ids(graph.declared_coordinating_system_ids)) {
        return invalid_argument("Source-law spell graph shape is invalid");
    }
    std::set<uint32_t> node_ids;
    bool has_intrinsic = false;
    bool has_output = false;
    for (const SourceLawSpellNode& node : graph.nodes) {
        if (node.stable_node_id == 0 || !node_ids.insert(node.stable_node_id).second ||
            !is_valid_source_law_spell_node_kind(node.kind) || !is_valid_id(node.definition_id) ||
            node.parameter_ids.size() > kMaxSpellNodeParameters ||
            !has_unique_valid_ids(node.parameter_ids)) {
            return invalid_argument("Source-law spell graph node is invalid");
        }
        has_intrinsic = has_intrinsic || node.kind == SourceLawSpellNodeKind::kBodyIntrinsic;
        has_output = has_output || node.kind == SourceLawSpellNodeKind::kOutput;
    }
    if (!has_intrinsic || !has_output) {
        return invalid_argument("Source-law spell graph must contain a body intrinsic and an output");
    }
    for (const SourceLawSpellLink& link : graph.links) {
        if (!node_ids.contains(link.from_node_id) || !node_ids.contains(link.to_node_id) ||
            !is_valid_id(link.from_port_id) || !is_valid_id(link.to_port_id)) {
            return invalid_argument("Source-law spell graph link is invalid");
        }
    }
    return {};
}

struct SpellPortView {
    SourceLawSpellPortType type = SourceLawSpellPortType::kCount;
    bool allows_multiple_links = false;
};

[[nodiscard]] std::optional<SpellPortView> find_intrinsic_port(
    const SourceLawIntrinsicDefinition& definition,
    bool output,
    const SourceLawId& port_id) {
    const std::vector<SourceLawSpellPortType>& types = output
        ? definition.output_port_types
        : definition.input_port_types;
    const std::string prefix = output ? "output." : "input.";
    for (size_t index = 0; index < types.size(); ++index) {
        if (port_id == prefix + std::to_string(index)) {
            return SpellPortView{.type = types[index], .allows_multiple_links = output};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SpellPortView> find_spell_node_port(
    const SourceLawSpellNodeDefinition& definition,
    bool output,
    const SourceLawId& port_id) {
    const std::vector<SourceLawSpellPortDefinition>& ports = output
        ? definition.output_ports
        : definition.input_ports;
    const auto found = std::find_if(ports.begin(), ports.end(), [&port_id](const auto& port) {
        return port.id == port_id;
    });
    if (found == ports.end()) return std::nullopt;
    return SpellPortView{
        .type = found->type,
        .allows_multiple_links = output || found->allows_multiple_links,
    };
}

[[nodiscard]] std::optional<SpellPortView> find_graph_node_port(
    const SourceLawContentSnapshot& snapshot,
    const SourceLawSpellNode& node,
    bool output,
    const SourceLawId& port_id) {
    if (node.kind == SourceLawSpellNodeKind::kBodyIntrinsic) {
        const SourceLawIntrinsicDefinition* intrinsic = snapshot.find_intrinsic(node.definition_id);
        return intrinsic == nullptr ? std::nullopt : find_intrinsic_port(*intrinsic, output, port_id);
    }
    const SourceLawSpellNodeDefinition* definition = snapshot.find_spell_node_definition(
        node.definition_id);
    if (definition == nullptr || definition->kind != node.kind) return std::nullopt;
    return find_spell_node_port(*definition, output, port_id);
}

[[nodiscard]] snt::core::Expected<void> validate_spell_graph_references(
    const SourceLawSpellGraph& graph,
    const SourceLawContentSnapshot& snapshot) {
    if (auto result = validate_spell_graph_shape(graph); !result) return result.error();

    std::map<uint32_t, const SourceLawSpellNode*> nodes;
    std::set<SourceLawId> path_core_nodes;
    for (const SourceLawSpellNode& node : graph.nodes) {
        nodes.emplace(node.stable_node_id, &node);
        if (node.kind == SourceLawSpellNodeKind::kBodyIntrinsic) {
            if (snapshot.find_intrinsic(node.definition_id) == nullptr) {
                return invalid_argument("Source-law spell graph references a missing body intrinsic");
            }
        } else {
            const SourceLawSpellNodeDefinition* definition = snapshot.find_spell_node_definition(
                node.definition_id);
            if (definition == nullptr || definition->kind != node.kind) {
                return invalid_argument("Source-law spell graph references an incompatible node definition");
            }
            if (node.kind == SourceLawSpellNodeKind::kPathCore) {
                path_core_nodes.insert(node.definition_id);
            }
        }
    }
    for (const SourceLawId& required_core : graph.required_path_core_ids) {
        if (!path_core_nodes.contains(required_core)) {
            return invalid_argument("Source-law spell graph requires a path core it does not contain");
        }
    }
    for (const SourceLawId& hybrid_id : graph.requested_hybrid_link_ids) {
        if (snapshot.find_hybrid_link(hybrid_id) == nullptr) {
            return invalid_argument("Source-law spell graph references a missing hybrid link");
        }
    }
    for (const SourceLawId& system_id : graph.declared_primary_system_ids) {
        if (snapshot.find_system(system_id) == nullptr) {
            return invalid_argument("Source-law spell graph declares an unknown primary system");
        }
    }
    for (const SourceLawId& system_id : graph.declared_coordinating_system_ids) {
        if (snapshot.find_system(system_id) == nullptr) {
            return invalid_argument("Source-law spell graph declares an unknown coordinating system");
        }
    }

    std::set<std::pair<uint32_t, SourceLawId>> occupied_inputs;
    std::set<std::tuple<uint32_t, SourceLawId, uint32_t, SourceLawId>> unique_links;
    for (const SourceLawSpellLink& link : graph.links) {
        const SourceLawSpellNode* from = nodes.at(link.from_node_id);
        const SourceLawSpellNode* to = nodes.at(link.to_node_id);
        const std::optional<SpellPortView> output = find_graph_node_port(
            snapshot, *from, true, link.from_port_id);
        const std::optional<SpellPortView> input = find_graph_node_port(
            snapshot, *to, false, link.to_port_id);
        if (!output || !input || output->type != input->type ||
            !unique_links.emplace(link.from_node_id, link.from_port_id, link.to_node_id,
                                  link.to_port_id).second ||
            (!input->allows_multiple_links &&
             !occupied_inputs.emplace(link.to_node_id, link.to_port_id).second)) {
            return invalid_argument("Source-law spell graph has an invalid port connection");
        }
    }
    return {};
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

    for (const auto& [id, tuning] : snapshot.tunings_) {
        if (id != tuning.id || !is_valid_id(id) || tuning.allowed_slots.empty() ||
            !is_unique_and_valid(tuning.allowed_slots, is_valid_source_organ_slot) ||
            !has_unique_valid_ids(tuning.required_organ_tags) ||
            !has_unique_valid_ids(tuning.added_tuning_tags) ||
            !has_unique_valid_ids(tuning.removed_tuning_tags) ||
            tuning.source_reserve_cost <= 0 ||
            !std::isfinite(tuning.contamination_reduction) ||
            tuning.contamination_reduction < 0.0F || tuning.contamination_reduction > 1.0F ||
            !std::isfinite(tuning.mutation_reduction) || tuning.mutation_reduction < 0.0F ||
            tuning.mutation_reduction > 100.0F || !std::isfinite(tuning.stability_delta) ||
            tuning.stability_delta < -100.0F || tuning.stability_delta > 100.0F ||
            !std::isfinite(tuning.maximum_mutation_before) ||
            tuning.maximum_mutation_before < 0.0F ||
            tuning.maximum_mutation_before > 100.0F) {
            return invalid_argument("Source-law tuning definition is invalid: " + id);
        }
        if (tuning.added_tuning_tags.empty() && tuning.removed_tuning_tags.empty() &&
            tuning.contamination_reduction == 0.0F && tuning.mutation_reduction == 0.0F &&
            tuning.stability_delta == 0.0F) {
            return invalid_argument("Source-law tuning definition has no effect: " + id);
        }
        for (const SourceLawId& tag : tuning.added_tuning_tags) {
            if (contains(tuning.removed_tuning_tags, tag)) {
                return invalid_argument("Source-law tuning adds and removes the same tag: " + id);
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

    for (const auto& [id, intrinsic] : snapshot.intrinsics_) {
        if (id != intrinsic.id) {
            return invalid_argument("Source-law intrinsic map key mismatches its definition: " + id);
        }
        if (auto result = validate_intrinsic(intrinsic); !result) return result.error();
    }

    for (const auto& [id, definition] : snapshot.spell_node_definitions_) {
        if (id != definition.id) {
            return invalid_argument("Source-law spell node map key mismatches its definition: " + id);
        }
        if (auto result = validate_spell_node_definition(definition); !result) {
            return result.error();
        }
    }

    for (const auto& [id, hybrid] : snapshot.hybrid_links_) {
        if (id != hybrid.id) {
            return invalid_argument("Source-law hybrid link map key mismatches its definition: " + id);
        }
        if (auto result = validate_hybrid_link(hybrid); !result) return result.error();
        for (const SourceLawId& intrinsic_id : hybrid.required_intrinsic_ids) {
            const SourceLawIntrinsicDefinition* intrinsic = snapshot.find_intrinsic(intrinsic_id);
            if (intrinsic == nullptr) {
                return invalid_argument("Source-law hybrid link references a missing intrinsic: " + id);
            }
            const bool supplies_a_required_system = std::any_of(
                intrinsic->required_closed_systems.begin(), intrinsic->required_closed_systems.end(),
                [&hybrid](const SourceBodySystem system) {
                    return contains(hybrid.required_distinct_systems, system);
                });
            if (!supplies_a_required_system) {
                return invalid_argument("Source-law hybrid link intrinsic has no required system overlap: " + id);
            }
        }
    }

    for (const auto& [id, graph] : snapshot.spell_graphs_) {
        if (id != graph.id || !is_valid_id(id) ||
            !has_unique_valid_ids(graph.compatible_path_ids) ||
            graph.graph.kind == SourceLawSpellGraphKind::kPlayerAuthored ||
            (graph.graph.kind == SourceLawSpellGraphKind::kPathCompletion &&
             !graph.requires_unification_circuit)) {
            return invalid_argument("Source-law spell graph definition is invalid: " + id);
        }
        if (graph.graph.kind == SourceLawSpellGraphKind::kPathAwakening ||
            graph.graph.kind == SourceLawSpellGraphKind::kPathSystem ||
            graph.graph.kind == SourceLawSpellGraphKind::kPathSignature ||
            graph.graph.kind == SourceLawSpellGraphKind::kPathCompletion) {
            if (graph.compatible_path_ids.empty()) {
                return invalid_argument("Source-law path spell graph has no compatible path: " + id);
            }
        }
        for (const SourceLawId& path_id : graph.compatible_path_ids) {
            if (snapshot.find_path(path_id) == nullptr) {
                return invalid_argument("Source-law spell graph references a missing path: " + id);
            }
        }
        if (auto result = validate_spell_graph_references(graph.graph, snapshot); !result) {
            return result.error();
        }
        if (auto result = SourceLawSpellCompiler::validate_graph(snapshot, graph.graph);
            !result) {
            return result.error();
        }
    }

    for (const auto& [id, system] : snapshot.systems_) {
        if (id != system.id || !is_valid_id(id) ||
            !is_valid_source_body_system(system.body_system) ||
            system.resonance_requirements.empty() || system.closure_requirements.empty() ||
            snapshot.find_reaction(system.elemental_reaction_id) == nullptr ||
            system.intrinsic_operation_ids.empty() ||
            !has_unique_valid_ids(system.intrinsic_operation_ids) ||
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
        for (const SourceLawId& intrinsic_id : system.intrinsic_operation_ids) {
            const SourceLawIntrinsicDefinition* intrinsic = snapshot.find_intrinsic(intrinsic_id);
            if (intrinsic == nullptr ||
                !contains(intrinsic->required_closed_systems, system.body_system)) {
                return invalid_argument("Source-law system exposes an incompatible intrinsic: " + id);
            }
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
            path.path_core_operation_ids.empty() ||
            path.awakening_spell_graph_ids.empty() ||
            path.system_spell_graph_ids.empty() ||
            path.signature_spell_graph_ids.empty() ||
            path.completion_spell_graph_ids.empty() ||
            !has_unique_valid_ids(path.path_core_operation_ids) ||
            !has_unique_valid_ids(path.awakening_spell_graph_ids) ||
            !has_unique_valid_ids(path.system_spell_graph_ids) ||
            !has_unique_valid_ids(path.signature_spell_graph_ids) ||
            !has_unique_valid_ids(path.completion_spell_graph_ids) ||
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
        for (const SourceLawId& operation_id : path.path_core_operation_ids) {
            const SourceLawSpellNodeDefinition* operation = snapshot.find_spell_node_definition(
                operation_id);
            if (operation == nullptr || operation->kind != SourceLawSpellNodeKind::kPathCore) {
                return invalid_argument("Source-law path references a missing path core operation: " + id);
            }
        }
        const auto validate_path_graphs = [&snapshot, &id](
                                              const std::vector<SourceLawId>& graph_ids,
                                              SourceLawSpellGraphKind expected_kind) {
            for (const SourceLawId& graph_id : graph_ids) {
                const SourceLawSpellGraphDefinition* graph = snapshot.find_spell_graph(graph_id);
                if (graph == nullptr || graph->graph.kind != expected_kind ||
                    !contains(graph->compatible_path_ids, id)) {
                    return false;
                }
            }
            return true;
        };
        if (!validate_path_graphs(path.awakening_spell_graph_ids,
                                  SourceLawSpellGraphKind::kPathAwakening) ||
            !validate_path_graphs(path.system_spell_graph_ids,
                                  SourceLawSpellGraphKind::kPathSystem) ||
            !validate_path_graphs(path.signature_spell_graph_ids,
                                  SourceLawSpellGraphKind::kPathSignature) ||
            !validate_path_graphs(path.completion_spell_graph_ids,
                                  SourceLawSpellGraphKind::kPathCompletion)) {
            return invalid_argument("Source-law path preset graph contract is invalid: " + id);
        }
    }

    for (const auto& [tool_id, tool] : snapshot.tool_spell_assemblies_) {
        if (tool_id != tool.tool_definition_id || !is_valid_id(tool_id) ||
            !has_unique_valid_ids(tool.allowed_rune_tags) ||
            !has_unique_valid_ids(tool.allowed_magic_charm_tags) ||
            !has_unique_valid_ids(tool.required_product_tags) ||
            !has_unique_valid_ids(tool.required_tool_interface_tags)) {
            return invalid_argument("Source-law tool spell assembly definition is invalid: " + tool_id);
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
            body_template.innate_spell_graph_ids.empty() ||
            !has_unique_valid_ids(body_template.innate_spell_graph_ids) ||
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
        for (const SourceLawId& graph_id : body_template.innate_spell_graph_ids) {
            const SourceLawSpellGraphDefinition* graph = snapshot.find_spell_graph(graph_id);
            if (graph == nullptr || graph->graph.kind != SourceLawSpellGraphKind::kCreatureInnate) {
                return invalid_argument("Source-law body template references a missing innate graph: " + id);
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

const SourceLawTuningDefinition* SourceLawContentSnapshot::find_tuning(
    const SourceLawId& id) const {
    return find_or_null(tunings_, id);
}

const ElementalReactionDefinition* SourceLawContentSnapshot::find_reaction(
    const SourceLawId& id) const {
    return find_or_null(reactions_, id);
}

const OrganSystemDefinition* SourceLawContentSnapshot::find_system(const SourceLawId& id) const {
    return find_or_null(systems_, id);
}

const SourceLawIntrinsicDefinition* SourceLawContentSnapshot::find_intrinsic(
    const SourceLawId& id) const {
    return find_or_null(intrinsics_, id);
}

const SourceLawHybridLinkDefinition* SourceLawContentSnapshot::find_hybrid_link(
    const SourceLawId& id) const {
    return find_or_null(hybrid_links_, id);
}

const SourceLawSpellNodeDefinition* SourceLawContentSnapshot::find_spell_node_definition(
    const SourceLawId& id) const {
    return find_or_null(spell_node_definitions_, id);
}

const SourceLawSpellGraphDefinition* SourceLawContentSnapshot::find_spell_graph(
    const SourceLawId& id) const {
    return find_or_null(spell_graphs_, id);
}

const ToolSpellAssemblyDefinition* SourceLawContentSnapshot::find_tool_spell_assembly(
    const SourceLawId& tool_definition_id) const {
    return find_or_null(tool_spell_assemblies_, tool_definition_id);
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

snt::core::Expected<void> SourceLawContentBuilder::add_tuning(
    SourceLawTuningDefinition definition) {
    return add_unique_definition(snapshot_.tunings_, std::move(definition), "tuning");
}

snt::core::Expected<void> SourceLawContentBuilder::add_reaction(
    ElementalReactionDefinition definition) {
    return add_unique_definition(snapshot_.reactions_, std::move(definition), "reaction");
}

snt::core::Expected<void> SourceLawContentBuilder::add_system(
    OrganSystemDefinition definition) {
    return add_unique_definition(snapshot_.systems_, std::move(definition), "system");
}

snt::core::Expected<void> SourceLawContentBuilder::add_intrinsic(
    SourceLawIntrinsicDefinition definition) {
    return add_unique_definition(snapshot_.intrinsics_, std::move(definition), "intrinsic");
}

snt::core::Expected<void> SourceLawContentBuilder::add_hybrid_link(
    SourceLawHybridLinkDefinition definition) {
    return add_unique_definition(snapshot_.hybrid_links_, std::move(definition), "hybrid link");
}

snt::core::Expected<void> SourceLawContentBuilder::add_spell_node_definition(
    SourceLawSpellNodeDefinition definition) {
    return add_unique_definition(snapshot_.spell_node_definitions_, std::move(definition),
                                 "spell node");
}

snt::core::Expected<void> SourceLawContentBuilder::add_spell_graph(
    SourceLawSpellGraphDefinition definition) {
    return add_unique_definition(snapshot_.spell_graphs_, std::move(definition), "spell graph");
}

snt::core::Expected<void> SourceLawContentBuilder::add_path(SourcePathDefinition definition) {
    return add_unique_definition(snapshot_.paths_, std::move(definition), "path");
}

snt::core::Expected<void> SourceLawContentBuilder::add_tool_spell_assembly(
    ToolSpellAssemblyDefinition definition) {
    if (!is_valid_id(definition.tool_definition_id)) {
        return invalid_argument("Source-law tool spell assembly id is invalid");
    }
    const SourceLawId id = definition.tool_definition_id;
    if (!snapshot_.tool_spell_assemblies_.emplace(id, std::move(definition)).second) {
        return invalid_argument("Duplicate source-law tool spell assembly id: " + id);
    }
    return {};
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
    SNT_LOG_INFO("Published source-law content snapshot revision=%llu organs=%zu tunings=%zu systems=%zu intrinsics=%zu graphs=%zu paths=%zu creatures=%zu",
                 static_cast<unsigned long long>(snapshot_.revision_), snapshot_.organs_.size(),
                 snapshot_.tunings_.size(), snapshot_.systems_.size(), snapshot_.intrinsics_.size(),
                 snapshot_.spell_graphs_.size(), snapshot_.paths_.size(),
                 snapshot_.creature_bodies_.size());
    return std::move(snapshot_);
}

}  // namespace snt::game::source_law
