// Deterministic source-law body evaluation implementation.

#define SNT_LOG_CHANNEL "game.source_law.evaluator"
#include "game/source_law/source_law_evaluator.h"

#include "core/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace snt::game::source_law {
namespace {

template <typename T>
[[nodiscard]] bool contains(const std::vector<T>& values, const T& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T, typename U>
void append_unique(std::vector<T>& values, U&& value) {
    T normalized{std::forward<U>(value)};
    if (!contains(values, normalized)) values.push_back(std::move(normalized));
}

[[nodiscard]] bool has_all(const std::vector<SourceLawId>& required,
                           const std::vector<SourceLawId>& available) {
    return std::all_of(required.begin(), required.end(), [&available](const auto& id) {
        return contains(available, id);
    });
}

[[nodiscard]] std::string system_reason(const SourceLawId& system_id,
                                        std::string_view phase,
                                        size_t index) {
    return "source_law.reason.system." + system_id + "." + std::string{phase} + "." +
           std::to_string(index);
}

[[nodiscard]] std::string bridge_reason(SourceBodyIntegrationBridge bridge,
                                        std::string_view kind,
                                        std::string_view value) {
    return "source_law.reason.integration." +
           std::string{source_body_integration_bridge_name(bridge)} + "." +
           std::string{kind} + "." + std::string{value};
}

struct InstalledOrgan {
    SourceOrganSlot slot = SourceOrganSlot::kCount;
    const OrganInstance* instance = nullptr;
    const OrganDefinition* definition = nullptr;
};

[[nodiscard]] bool is_usable_organ(const InstalledOrgan& organ) {
    return organ.instance != nullptr && organ.definition != nullptr &&
           is_valid_source_organ_slot(organ.slot) && organ.definition->slot == organ.slot &&
           std::isfinite(organ.instance->integrity) && organ.instance->integrity > 0.0F &&
           std::isfinite(organ.instance->contamination) &&
           organ.instance->contamination >= 0.0F && organ.instance->contamination <= 1.0F;
}

[[nodiscard]] std::vector<InstalledOrgan> collect_installed_organs(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyState& body,
    std::vector<SourceLawId>& body_blocking_reasons) {
    std::vector<InstalledOrgan> result;
    result.reserve(kSourceOrganSlotCount);
    for (size_t index = 0; index < kSourceOrganSlotCount; ++index) {
        const std::optional<OrganInstance>& instance = body.organs[index];
        if (!instance) continue;
        const SourceOrganSlot slot = static_cast<SourceOrganSlot>(index);
        const OrganDefinition* definition = content.find_organ(instance->definition_id);
        InstalledOrgan installed{.slot = slot, .instance = &*instance, .definition = definition};
        if (definition == nullptr) {
            append_unique(body_blocking_reasons,
                          "source_law.reason.body.missing_organ_definition." +
                              std::to_string(index));
            continue;
        }
        if (definition->slot != slot) {
            append_unique(body_blocking_reasons,
                          "source_law.reason.body.organ_slot_mismatch." +
                              std::to_string(index));
            continue;
        }
        if (!is_usable_organ(installed)) {
            append_unique(body_blocking_reasons,
                          "source_law.reason.body.unusable_organ." + std::to_string(index));
            continue;
        }
        result.push_back(installed);
    }
    return result;
}

[[nodiscard]] bool organ_has_tag(const InstalledOrgan& organ, const SourceLawId& tag) {
    if (!is_usable_organ(organ)) return false;
    return contains(organ.definition->bloodline_tags, tag) ||
           contains(organ.definition->ecology_tags, tag) ||
           contains(organ.definition->system_tags, tag) ||
           contains(organ.definition->native_path_tags, tag) ||
           contains(organ.definition->pressure_tags, tag) ||
           contains(organ.instance->tuning_tags, tag);
}

[[nodiscard]] bool organ_has_any_role(const InstalledOrgan& organ,
                                      const std::vector<OrganSystemRole>& roles) {
    return roles.empty() || std::any_of(roles.begin(), roles.end(), [&organ](const auto role) {
        return contains(organ.definition->roles, role);
    });
}

[[nodiscard]] bool organ_matches_requirement(const InstalledOrgan& organ,
                                              const OrganSystemRequirement& requirement) {
    if (!is_usable_organ(organ)) return false;
    if (!requirement.allowed_slots.empty() &&
        !contains(requirement.allowed_slots, organ.slot)) {
        return false;
    }
    if (!organ_has_any_role(organ, requirement.required_roles)) return false;
    return std::all_of(requirement.required_tags.begin(), requirement.required_tags.end(),
                       [&organ](const auto& tag) { return organ_has_tag(organ, tag); });
}

struct RequirementEvaluation {
    bool is_satisfied = false;
    std::vector<SourceOrganSlot> matching_slots;
};

[[nodiscard]] RequirementEvaluation evaluate_requirement(
    const OrganSystemRequirement& requirement,
    const std::vector<InstalledOrgan>& organs) {
    RequirementEvaluation report;
    for (const InstalledOrgan& organ : organs) {
        if (organ_matches_requirement(organ, requirement)) {
            report.matching_slots.push_back(organ.slot);
        }
    }
    report.is_satisfied = report.matching_slots.size() >= requirement.minimum_matches;
    return report;
}

[[nodiscard]] bool context_has_all_ecology(const std::vector<SourceLawId>& required,
                                            const SourceLawEvaluationContext& context) {
    return has_all(required, context.satisfied_ecology_tags);
}

struct ReactionCandidate {
    SourceLawContributionReference reference;
    const ElementalPhysiologyRule* rule = nullptr;
};

[[nodiscard]] std::vector<ReactionCandidate> candidates_for_step(
    const ElementalReactionStepDefinition& step,
    const std::vector<InstalledOrgan>& organs,
    const SourceLawContentSnapshot& content) {
    std::vector<ReactionCandidate> result;
    for (const InstalledOrgan& organ : organs) {
        for (size_t contribution_index = 0;
             contribution_index < organ.definition->elemental_contributions.size();
             ++contribution_index) {
            if (contribution_index > std::numeric_limits<uint16_t>::max()) break;
            const OrganElementalContribution& contribution =
                organ.definition->elemental_contributions[contribution_index];
            if (contribution.stage != step.stage ||
                !contains(step.allowed_actions, contribution.action) ||
                !contains(step.allowed_elements, contribution.element)) {
                continue;
            }
            const ElementalPhysiologyRule* rule = content.find_element_rule(contribution.element);
            if (rule == nullptr || !contains(rule->allowed_actions, contribution.action) ||
                !contains(rule->allowed_stages, contribution.stage)) {
                continue;
            }
            result.push_back({
                .reference = {
                    .slot = organ.slot,
                    .organ_definition_id = organ.definition->id,
                    .element = contribution.element,
                    .action = contribution.action,
                    .stage = contribution.stage,
                    .contribution_index = static_cast<uint16_t>(contribution_index),
                },
                .rule = rule,
            });
        }
    }
    return result;
}

using CandidateGroup = std::vector<size_t>;

void make_candidate_groups(const std::vector<ReactionCandidate>& candidates,
                           uint8_t required_count,
                           size_t start,
                           CandidateGroup& pending,
                           std::vector<CandidateGroup>& result) {
    if (pending.size() == required_count) {
        result.push_back(pending);
        return;
    }
    const size_t remaining_needed = static_cast<size_t>(required_count) - pending.size();
    if (candidates.size() - start < remaining_needed) return;
    for (size_t index = start; index < candidates.size(); ++index) {
        const SourceOrganSlot slot = candidates[index].reference.slot;
        const bool duplicate_slot = std::any_of(pending.begin(), pending.end(),
                                                [&candidates, slot](const size_t existing) {
            return candidates[existing].reference.slot == slot;
        });
        if (duplicate_slot) continue;
        pending.push_back(index);
        make_candidate_groups(candidates, required_count, index + 1, pending, result);
        pending.pop_back();
    }
}

[[nodiscard]] std::vector<CandidateGroup> make_candidate_groups(
    const std::vector<ReactionCandidate>& candidates, uint8_t required_count) {
    std::vector<CandidateGroup> result;
    CandidateGroup pending;
    make_candidate_groups(candidates, required_count, 0, pending, result);
    return result;
}

[[nodiscard]] bool group_follows(const std::vector<ReactionCandidate>& previous,
                                  const std::vector<ReactionCandidate>& current) {
    if (previous.empty()) return true;
    for (const ReactionCandidate& candidate : current) {
        const bool follows_any_previous = std::any_of(
            previous.begin(), previous.end(), [&candidate](const ReactionCandidate& earlier) {
                return earlier.rule != nullptr &&
                       contains(earlier.rule->allowed_next_actions, candidate.reference.action);
            });
        if (!follows_any_previous) return false;
    }
    return true;
}

[[nodiscard]] bool groups_are_distinct(const std::vector<ReactionCandidate>& previous,
                                       const std::vector<ReactionCandidate>& current) {
    for (const ReactionCandidate& current_candidate : current) {
        const bool reused = std::any_of(previous.begin(), previous.end(),
                                        [&current_candidate](const auto& previous_candidate) {
            return previous_candidate.reference.slot == current_candidate.reference.slot;
        });
        if (reused) return false;
    }
    return true;
}

struct ReactionSolve {
    bool is_complete = false;
    std::vector<std::vector<ReactionCandidate>> selections;
};

[[nodiscard]] ReactionSolve solve_steps(
    const std::vector<ElementalReactionStepDefinition>& steps,
    const std::vector<std::vector<ReactionCandidate>>& candidates_by_step,
    const std::vector<ReactionCandidate>& initial_previous = {}) {
    ReactionSolve result;
    if (steps.empty()) {
        result.is_complete = true;
        return result;
    }
    std::vector<std::vector<ReactionCandidate>> selections(steps.size());
    std::function<bool(size_t, const std::vector<ReactionCandidate>&)> visit;
    visit = [&](size_t step_index, const std::vector<ReactionCandidate>& previous) {
        if (step_index == steps.size()) return true;
        const ElementalReactionStepDefinition& step = steps[step_index];
        const std::vector<ReactionCandidate>& candidates = candidates_by_step[step_index];
        for (const CandidateGroup& group : make_candidate_groups(candidates,
                                                                  step.minimum_contributors)) {
            std::vector<ReactionCandidate> current;
            current.reserve(group.size());
            for (const size_t candidate_index : group) current.push_back(candidates[candidate_index]);
            if (step.requires_distinct_organ_from_previous_step &&
                !groups_are_distinct(previous, current)) {
                continue;
            }
            if (!group_follows(previous, current)) continue;
            selections[step_index] = std::move(current);
            if (visit(step_index + 1, selections[step_index])) return true;
        }
        return false;
    };
    result.is_complete = visit(0, initial_previous);
    if (result.is_complete) result.selections = std::move(selections);
    return result;
}

void append_competing_steps(const std::vector<ElementalReactionStepDefinition>& steps,
                            const std::vector<std::vector<ReactionCandidate>>& candidates_by_step,
                            std::vector<SourceLawId>& competing_step_ids) {
    for (size_t step_index = 0; step_index < steps.size(); ++step_index) {
        const auto& candidates = candidates_by_step[step_index];
        bool competing = false;
        for (size_t first = 0; first < candidates.size() && !competing; ++first) {
            for (size_t second = first + 1; second < candidates.size(); ++second) {
                const auto& first_candidate = candidates[first];
                const auto& second_candidate = candidates[second];
                if (first_candidate.reference.slot == second_candidate.reference.slot) continue;
                if ((first_candidate.rule != nullptr &&
                     contains(first_candidate.rule->competing_actions,
                              second_candidate.reference.action)) ||
                    (second_candidate.rule != nullptr &&
                     contains(second_candidate.rule->competing_actions,
                              first_candidate.reference.action))) {
                    competing = true;
                    break;
                }
            }
        }
        if (competing) append_unique(competing_step_ids, steps[step_index].id);
    }
}

void append_step_reports(const std::vector<ElementalReactionStepDefinition>& steps,
                         const std::vector<std::vector<ReactionCandidate>>& candidates_by_step,
                         const ReactionSolve& solve,
                         std::vector<ElementalReactionStepReport>& reports,
                         std::vector<SourceLawId>& missing_step_ids) {
    reports.reserve(reports.size() + steps.size());
    for (size_t index = 0; index < steps.size(); ++index) {
        ElementalReactionStepReport report{.step_id = steps[index].id};
        if (solve.is_complete) {
            report.is_satisfied = true;
            for (const ReactionCandidate& candidate : solve.selections[index]) {
                report.contributors.push_back(candidate.reference);
            }
        } else {
            const auto groups = make_candidate_groups(candidates_by_step[index],
                                                       steps[index].minimum_contributors);
            if (groups.empty()) append_unique(missing_step_ids, steps[index].id);
        }
        reports.push_back(std::move(report));
    }
}

[[nodiscard]] bool body_has_relief_tag(const std::vector<InstalledOrgan>& organs,
                                       const SourceLawId& tag) {
    return std::any_of(organs.begin(), organs.end(), [&tag](const InstalledOrgan& organ) {
        return organ_has_tag(organ, tag);
    });
}

void append_unresolved_byproduct(std::vector<SourceLawId>& unresolved,
                                 const std::vector<SourceLawId>& product_tags,
                                 const SourceLawId& fallback_tag) {
    if (product_tags.empty()) {
        append_unique(unresolved, fallback_tag);
        return;
    }
    for (const SourceLawId& tag : product_tags) append_unique(unresolved, tag);
}

[[nodiscard]] ElementalReactionReport evaluate_reaction(
    const ElementalReactionDefinition& reaction,
    const SourceLawContentSnapshot& content,
    const std::vector<InstalledOrgan>& organs) {
    ElementalReactionReport report{.reaction_id = reaction.id};
    std::vector<std::vector<ReactionCandidate>> closure_candidates;
    closure_candidates.reserve(reaction.closure_steps.size());
    for (const ElementalReactionStepDefinition& step : reaction.closure_steps) {
        closure_candidates.push_back(candidates_for_step(step, organs, content));
    }
    const ReactionSolve closure = solve_steps(reaction.closure_steps, closure_candidates);
    append_step_reports(reaction.closure_steps, closure_candidates, closure,
                        report.closure_steps, report.missing_step_ids);
    append_competing_steps(reaction.closure_steps, closure_candidates,
                           report.competing_step_ids);
    report.is_continuous = closure.is_complete;
    if (!closure.is_complete) return report;

    std::vector<ReactionCandidate> last_closure;
    if (!closure.selections.empty()) last_closure = closure.selections.back();
    std::vector<std::vector<ReactionCandidate>> growth_candidates;
    growth_candidates.reserve(reaction.growth_steps.size());
    for (const ElementalReactionStepDefinition& step : reaction.growth_steps) {
        growth_candidates.push_back(candidates_for_step(step, organs, content));
    }
    const ReactionSolve growth = solve_steps(reaction.growth_steps, growth_candidates,
                                             last_closure);
    append_step_reports(reaction.growth_steps, growth_candidates, growth,
                        report.growth_steps, report.missing_step_ids);
    append_competing_steps(reaction.growth_steps, growth_candidates,
                           report.competing_step_ids);
    report.growth_chain_continuous = growth.is_complete;

    for (const SourceLawId& relief_tag : reaction.required_relief_tags) {
        if (!body_has_relief_tag(organs, relief_tag)) {
            append_unresolved_byproduct(report.unresolved_byproduct_tags,
                                        reaction.byproduct_tags, relief_tag);
        }
    }
    const auto evaluate_selected_byproducts = [&report, &organs, &content](
                                                 const std::vector<std::vector<ReactionCandidate>>&
                                                     selections) {
        for (const auto& selection : selections) {
            for (const ReactionCandidate& candidate : selection) {
                const OrganDefinition* organ = content.find_organ(
                    candidate.reference.organ_definition_id);
                if (organ == nullptr || candidate.reference.contribution_index >=
                                          organ->elemental_contributions.size()) {
                    continue;
                }
                const OrganElementalContribution& contribution =
                    organ->elemental_contributions[candidate.reference.contribution_index];
                for (const SourceLawId& byproduct_tag : contribution.byproduct_tags) {
                    if (!body_has_relief_tag(organs, byproduct_tag)) {
                        append_unique(report.unresolved_byproduct_tags, byproduct_tag);
                    }
                }
                if (candidate.rule != nullptr) {
                    for (const SourceLawId& buffer_tag : candidate.rule->required_buffer_tags) {
                        if (!body_has_relief_tag(organs, buffer_tag)) {
                            append_unique(report.unresolved_byproduct_tags, buffer_tag);
                        }
                    }
                }
            }
        }
    };
    evaluate_selected_byproducts(closure.selections);
    if (growth.is_complete) evaluate_selected_byproducts(growth.selections);
    report.all_byproducts_resolved = report.unresolved_byproduct_tags.empty();
    return report;
}

[[nodiscard]] bool all_requirements_met(const std::vector<OrganSystemRequirement>& requirements,
                                        const std::vector<InstalledOrgan>& organs,
                                        const SourceLawId& system_id,
                                        std::string_view phase,
                                        std::vector<SourceOrganSlot>& contributing_slots,
                                        std::vector<SourceLawId>& reasons) {
    bool all_met = true;
    for (size_t index = 0; index < requirements.size(); ++index) {
        const RequirementEvaluation match = evaluate_requirement(requirements[index], organs);
        for (const SourceOrganSlot slot : match.matching_slots) {
            append_unique(contributing_slots, slot);
        }
        if (!match.is_satisfied) {
            all_met = false;
            append_unique(reasons, system_reason(system_id, phase, index));
        }
    }
    return all_met;
}

void append_reaction_slots(const ElementalReactionReport& reaction,
                           std::vector<SourceOrganSlot>& slots) {
    const auto append_from_steps = [&slots](const std::vector<ElementalReactionStepReport>& steps) {
        for (const ElementalReactionStepReport& step : steps) {
            for (const SourceLawContributionReference& contributor : step.contributors) {
                append_unique(slots, contributor.slot);
            }
        }
    };
    append_from_steps(reaction.closure_steps);
    append_from_steps(reaction.growth_steps);
}

[[nodiscard]] bool system_has_applied_path_action(
    const SourceLawSystemReport& system,
    const SourcePathReactionPreference& preference) {
    const auto has_step = [&preference](const std::vector<ElementalReactionStepReport>& steps) {
        return std::any_of(steps.begin(), steps.end(), [&preference](const auto& step) {
            return std::any_of(step.contributors.begin(), step.contributors.end(),
                               [&preference](const auto& contributor) {
                return contributor.stage == preference.stage &&
                       contributor.action == preference.action;
            });
        });
    };
    return has_step(system.reaction.closure_steps) || has_step(system.reaction.growth_steps);
}

[[nodiscard]] bool bridge_has_role(const SourceBodyIntegrationBridgeDefinition& bridge,
                                   const std::array<const InstalledOrgan*, kSourceOrganSlotCount>& by_slot,
                                   OrganSystemRole required_role) {
    return std::any_of(bridge.required_slots.begin(), bridge.required_slots.end(),
                       [&by_slot, required_role](const SourceOrganSlot slot) {
        const InstalledOrgan* organ = by_slot[static_cast<size_t>(slot)];
        return organ != nullptr && contains(organ->definition->roles, required_role);
    });
}

[[nodiscard]] bool bridge_has_tag(const SourceBodyIntegrationBridgeDefinition& bridge,
                                  const std::array<const InstalledOrgan*, kSourceOrganSlotCount>& by_slot,
                                  const SourceLawId& required_tag) {
    return std::any_of(bridge.required_slots.begin(), bridge.required_slots.end(),
                       [&by_slot, &required_tag](const SourceOrganSlot slot) {
        const InstalledOrgan* organ = by_slot[static_cast<size_t>(slot)];
        return organ != nullptr && organ_has_tag(*organ, required_tag);
    });
}

[[nodiscard]] bool bridge_has_stage(const SourceBodyIntegrationBridgeDefinition& bridge,
                                    const std::array<const InstalledOrgan*, kSourceOrganSlotCount>& by_slot,
                                    ElementalReactionStage required_stage) {
    return std::any_of(bridge.required_slots.begin(), bridge.required_slots.end(),
                       [&by_slot, required_stage](const SourceOrganSlot slot) {
        const InstalledOrgan* organ = by_slot[static_cast<size_t>(slot)];
        if (organ == nullptr) return false;
        return std::any_of(organ->definition->elemental_contributions.begin(),
                           organ->definition->elemental_contributions.end(),
                           [required_stage](const auto& contribution) {
            return contribution.stage == required_stage;
        });
    });
}

[[nodiscard]] bool bridge_has_action(const SourceBodyIntegrationBridgeDefinition& bridge,
                                     const std::array<const InstalledOrgan*, kSourceOrganSlotCount>& by_slot,
                                     ElementalPhysiologyAction required_action) {
    return std::any_of(bridge.required_slots.begin(), bridge.required_slots.end(),
                       [&by_slot, required_action](const SourceOrganSlot slot) {
        const InstalledOrgan* organ = by_slot[static_cast<size_t>(slot)];
        if (organ == nullptr) return false;
        return std::any_of(organ->definition->elemental_contributions.begin(),
                           organ->definition->elemental_contributions.end(),
                           [required_action](const auto& contribution) {
            return contribution.action == required_action;
        });
    });
}

class SlotDisjointSet final {
public:
    SlotDisjointSet() {
        for (size_t index = 0; index < parent_.size(); ++index) parent_[index] = index;
    }

    void unite(SourceOrganSlot first, SourceOrganSlot second) {
        const size_t first_root = find(static_cast<size_t>(first));
        const size_t second_root = find(static_cast<size_t>(second));
        if (first_root != second_root) parent_[second_root] = first_root;
    }

    [[nodiscard]] bool connected(SourceOrganSlot first, SourceOrganSlot second) {
        return find(static_cast<size_t>(first)) == find(static_cast<size_t>(second));
    }

private:
    [[nodiscard]] size_t find(size_t value) {
        if (parent_[value] == value) return value;
        parent_[value] = find(parent_[value]);
        return parent_[value];
    }

    std::array<size_t, kSourceOrganSlotCount> parent_{};
};

void unite_slots(SlotDisjointSet& sets, const std::vector<SourceOrganSlot>& slots) {
    if (slots.size() < 2) return;
    for (size_t index = 1; index < slots.size(); ++index) sets.unite(slots[0], slots[index]);
}

[[nodiscard]] SourceCircuitScheduleReport evaluate_schedule(
    const SourceCircuitSchedule& schedule,
    const std::vector<SourceLawSystemReport>& systems,
    std::vector<SourceLawId>& body_blocking_reasons) {
    SourceCircuitScheduleReport report{.effective_schedule = schedule};
    std::set<SourceLawId> runnable;
    for (const SourceLawSystemReport& system : systems) {
        if (system.state == SourceLawSystemState::kClosed ||
            system.state == SourceLawSystemState::kGrowing) {
            runnable.insert(system.system_id);
        }
    }
    if (report.effective_schedule.current_primary_circuit_system_id &&
        !runnable.contains(*report.effective_schedule.current_primary_circuit_system_id)) {
        report.primary_circuit_is_valid = false;
        append_unique(report.rejected_circuit_system_ids,
                      *report.effective_schedule.current_primary_circuit_system_id);
        append_unique(body_blocking_reasons, "source_law.reason.schedule.primary_not_closed");
        report.effective_schedule.current_primary_circuit_system_id.reset();
    }
    std::vector<SourceLawId> valid_coordinating;
    std::set<SourceLawId> seen;
    for (const SourceLawId& system_id : schedule.coordinating_circuit_system_ids) {
        if (!runnable.contains(system_id) ||
            (report.effective_schedule.current_primary_circuit_system_id &&
             *report.effective_schedule.current_primary_circuit_system_id == system_id) ||
            !seen.insert(system_id).second) {
            append_unique(report.rejected_circuit_system_ids, system_id);
            continue;
        }
        valid_coordinating.push_back(system_id);
    }
    report.effective_schedule.coordinating_circuit_system_ids = std::move(valid_coordinating);
    if (!std::isfinite(report.effective_schedule.primary_circuit_reallocation_cooldown_seconds) ||
        report.effective_schedule.primary_circuit_reallocation_cooldown_seconds < 0.0F) {
        report.effective_schedule.primary_circuit_reallocation_cooldown_seconds = 0.0F;
        report.primary_circuit_is_valid = false;
        append_unique(body_blocking_reasons, "source_law.reason.schedule.invalid_cooldown");
    }
    return report;
}

}  // namespace

SourceLawEvaluation SourceLawBodyEvaluator::evaluate(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyState& body,
    const SourceLawEvaluationContext& context) {
    SourceLawEvaluation evaluation;
    const std::vector<InstalledOrgan> organs = collect_installed_organs(
        content, body, evaluation.body_blocking_reason_ids);

    for (const auto& [system_id, definition] : content.systems()) {
        SourceLawSystemReport report{
            .system_id = system_id,
            .body_system = definition.body_system,
        };
        report.resonance_requirements_met = all_requirements_met(
            definition.resonance_requirements, organs, system_id, "resonance",
            report.contributing_slots, report.blocking_reason_ids);
        report.closure_requirements_met = all_requirements_met(
            definition.closure_requirements, organs, system_id, "closure",
            report.contributing_slots, report.blocking_reason_ids);
        report.growth_link_requirements_met = all_requirements_met(
            definition.growth_link_requirements, organs, system_id, "growth",
            report.contributing_slots, report.blocking_reason_ids);
        report.ecology_conditions_met = context_has_all_ecology(definition.ecology_conditions,
                                                                  context);
        if (!report.ecology_conditions_met) {
            append_unique(report.blocking_reason_ids,
                          "source_law.reason.system." + system_id + ".ecology");
        }

        const ElementalReactionDefinition* reaction = content.find_reaction(
            definition.elemental_reaction_id);
        if (reaction != nullptr) report.reaction = evaluate_reaction(*reaction, content, organs);
        append_reaction_slots(report.reaction, report.contributing_slots);
        if (!report.reaction.is_continuous) {
            append_unique(report.blocking_reason_ids,
                          "source_law.reason.system." + system_id + ".reaction_not_continuous");
        }
        if (!report.reaction.all_byproducts_resolved) {
            append_unique(report.blocking_reason_ids,
                          "source_law.reason.system." + system_id + ".byproduct_unresolved");
        }

        const bool is_closed = report.resonance_requirements_met &&
                               report.closure_requirements_met &&
                               report.ecology_conditions_met &&
                               report.reaction.is_continuous &&
                               report.reaction.all_byproducts_resolved;
        const bool declares_growth_link = !definition.growth_link_requirements.empty() ||
                                          !reaction->growth_steps.empty();
        if (is_closed && declares_growth_link && report.growth_link_requirements_met &&
            report.reaction.growth_chain_continuous) {
            report.state = SourceLawSystemState::kGrowing;
        } else if (is_closed) {
            report.state = SourceLawSystemState::kClosed;
        } else if (report.resonance_requirements_met) {
            report.state = SourceLawSystemState::kResonant;
        }
        evaluation.systems.push_back(std::move(report));
    }

    if (!body.active_path_id.empty()) {
        evaluation.path.active_path_id = body.active_path_id;
        const SourcePathDefinition* path = content.find_path(body.active_path_id);
        if (path == nullptr) {
            evaluation.path.definition_exists = false;
            append_unique(evaluation.path.blocking_reason_ids,
                          "source_law.reason.path.definition_missing");
        } else {
            evaluation.path.core_organ_tags_met = std::all_of(
                path->core_organ_tags.begin(), path->core_organ_tags.end(),
                [&organs](const SourceLawId& tag) {
                    return std::any_of(organs.begin(), organs.end(), [&tag](const auto& organ) {
                        return organ_has_tag(organ, tag);
                    });
                });
            if (!evaluation.path.core_organ_tags_met) {
                append_unique(evaluation.path.blocking_reason_ids,
                              "source_law.reason.path.core_organ_tags");
            }
            for (const SourceLawSystemReport& system : evaluation.systems) {
                if (system.state != SourceLawSystemState::kClosed &&
                    system.state != SourceLawSystemState::kGrowing) {
                    continue;
                }
                if (!contains(path->preferred_systems, system.body_system)) continue;
                evaluation.path.preferred_system_is_closed = true;
                for (const SourcePathReactionPreference& preference :
                     path->reaction_preferences) {
                    if (preference.body_system != system.body_system ||
                        !system_has_applied_path_action(system, preference)) {
                        continue;
                    }
                    const SourceLawPathReactionApplication application{
                        .body_system = preference.body_system,
                        .stage = preference.stage,
                        .action = preference.action,
                        .product_modifier_id = preference.product_modifier_id,
                        .byproduct_handling_modifier_id =
                            preference.byproduct_handling_modifier_id,
                    };
                    const bool already_applied = std::any_of(
                        evaluation.path.applied_reaction_preferences.begin(),
                        evaluation.path.applied_reaction_preferences.end(),
                        [&application](const auto& existing) {
                            return existing.body_system == application.body_system &&
                                   existing.stage == application.stage &&
                                   existing.action == application.action;
                        });
                    if (!already_applied) {
                        evaluation.path.applied_reaction_preferences.push_back(application);
                    }
                }
            }
            if (!evaluation.path.preferred_system_is_closed) {
                append_unique(evaluation.path.blocking_reason_ids,
                              "source_law.reason.path.preferred_system_not_closed");
            }
            if (evaluation.path.applied_reaction_preferences.empty()) {
                append_unique(evaluation.path.blocking_reason_ids,
                              "source_law.reason.path.no_existing_reaction_action");
            }
            evaluation.path.is_resonant = evaluation.path.core_organ_tags_met &&
                                          evaluation.path.preferred_system_is_closed &&
                                          !evaluation.path.applied_reaction_preferences.empty();
        }
    }

    evaluation.circuit_schedule = evaluate_schedule(body.circuit_schedule, evaluation.systems,
                                                     evaluation.body_blocking_reason_ids);

    std::array<const InstalledOrgan*, kSourceOrganSlotCount> organs_by_slot{};
    for (const InstalledOrgan& organ : organs) {
        organs_by_slot[static_cast<size_t>(organ.slot)] = &organ;
        const float capacity_factor = std::clamp(organ.instance->integrity, 0.0F, 1.0F) *
                                      (1.0F - std::clamp(organ.instance->contamination, 0.0F, 1.0F));
        for (const OrganElementalContribution& contribution :
             organ.definition->elemental_contributions) {
            evaluation.derived_source_throughput += contribution.base_capacity * capacity_factor;
        }
        evaluation.derived_mental_load += organ.definition->base_mental_load;
    }
    if (!std::isfinite(evaluation.derived_source_throughput) ||
        evaluation.derived_source_throughput < 0.0F) {
        evaluation.derived_source_throughput = 0.0F;
        append_unique(evaluation.body_blocking_reason_ids,
                      "source_law.reason.body.invalid_throughput");
    }
    constexpr float kMaxDerivedSourceThroughput = 1'000'000.0F;
    evaluation.derived_source_throughput = std::min(evaluation.derived_source_throughput,
                                                     kMaxDerivedSourceThroughput);
    evaluation.derived_mana_max = static_cast<int32_t>(std::floor(
        evaluation.derived_source_throughput));

    SourceBodyIntegrationReport integration;
    const SourceBodyIntegrationDefinition* integration_definition = content.integration_definition();
    integration.all_slots_sublimated = organs.size() == kSourceOrganSlotCount;
    if (!integration.all_slots_sublimated) {
        append_unique(integration.blocking_reason_ids,
                      "source_law.reason.integration.incomplete_slots");
    }
    std::array<bool, kSourceBodyIntegrationBridgeCount> bridge_satisfied{};
    if (integration_definition != nullptr) {
        for (size_t index = 0; index < kSourceBodyIntegrationBridgeCount; ++index) {
            const SourceBodyIntegrationBridgeDefinition& bridge =
                integration_definition->required_bridges[index];
            SourceBodyIntegrationBridgeReport bridge_report{.bridge = bridge.bridge};
            for (const SourceOrganSlot slot : bridge.required_slots) {
                if (organs_by_slot[static_cast<size_t>(slot)] == nullptr) {
                    append_unique(bridge_report.blocking_reason_ids,
                                  bridge_reason(bridge.bridge, "missing_slot",
                                                source_organ_slot_name(slot)));
                }
            }
            for (const OrganSystemRole role : bridge.required_roles) {
                if (!bridge_has_role(bridge, organs_by_slot, role)) {
                    append_unique(bridge_report.blocking_reason_ids,
                                  bridge_reason(bridge.bridge, "missing_role",
                                                organ_system_role_name(role)));
                }
            }
            for (const SourceLawId& tag : bridge.required_tags) {
                if (!bridge_has_tag(bridge, organs_by_slot, tag)) {
                    append_unique(bridge_report.blocking_reason_ids,
                                  bridge_reason(bridge.bridge, "missing_tag", tag));
                }
            }
            for (const ElementalReactionStage stage : bridge.required_reaction_stages) {
                if (!bridge_has_stage(bridge, organs_by_slot, stage)) {
                    append_unique(bridge_report.blocking_reason_ids,
                                  bridge_reason(bridge.bridge, "missing_stage",
                                                elemental_reaction_stage_name(stage)));
                }
            }
            for (const ElementalPhysiologyAction action : bridge.required_actions) {
                if (!bridge_has_action(bridge, organs_by_slot, action)) {
                    append_unique(bridge_report.blocking_reason_ids,
                                  bridge_reason(bridge.bridge, "missing_action",
                                                elemental_physiology_action_name(action)));
                }
            }
            bridge_report.is_satisfied = bridge_report.blocking_reason_ids.empty();
            bridge_satisfied[index] = bridge_report.is_satisfied;
            integration.bridges[index] = std::move(bridge_report);
        }
    } else {
        append_unique(integration.blocking_reason_ids,
                      "source_law.reason.integration.definition_missing");
    }
    integration.energy_and_refinement_bridge =
        bridge_satisfied[static_cast<size_t>(SourceBodyIntegrationBridge::kEnergyAndRefinement)];
    integration.control_and_feedback_bridge =
        bridge_satisfied[static_cast<size_t>(SourceBodyIntegrationBridge::kControlAndFeedback)];
    integration.environment_and_barrier_bridge =
        bridge_satisfied[static_cast<size_t>(SourceBodyIntegrationBridge::kEnvironmentAndBarrier)];
    integration.load_and_action_bridge =
        bridge_satisfied[static_cast<size_t>(SourceBodyIntegrationBridge::kLoadAndAction)];
    for (const SourceBodyIntegrationBridgeReport& bridge : integration.bridges) {
        for (const SourceLawId& reason : bridge.blocking_reason_ids) {
            append_unique(integration.blocking_reason_ids, reason);
        }
    }

    SlotDisjointSet connected_slots;
    std::array<bool, kSourceOrganSlotCount> covered_by_closed_reaction{};
    bool any_closed_system = false;
    for (const SourceLawSystemReport& system : evaluation.systems) {
        if (system.state != SourceLawSystemState::kClosed &&
            system.state != SourceLawSystemState::kGrowing) {
            continue;
        }
        any_closed_system = true;
        unite_slots(connected_slots, system.contributing_slots);
        const auto cover_steps = [&covered_by_closed_reaction](
                                     const std::vector<ElementalReactionStepReport>& steps) {
            for (const ElementalReactionStepReport& step : steps) {
                for (const SourceLawContributionReference& contributor : step.contributors) {
                    covered_by_closed_reaction[static_cast<size_t>(contributor.slot)] = true;
                }
            }
        };
        cover_steps(system.reaction.closure_steps);
        cover_steps(system.reaction.growth_steps);
    }
    if (integration_definition != nullptr) {
        for (size_t index = 0; index < kSourceBodyIntegrationBridgeCount; ++index) {
            if (!bridge_satisfied[index]) continue;
            unite_slots(connected_slots,
                        integration_definition->required_bridges[index].required_slots);
        }
    }
    if (integration.all_slots_sublimated) {
        integration.is_single_connected_network = true;
        for (size_t index = 1; index < kSourceOrganSlotCount; ++index) {
            if (!connected_slots.connected(SourceOrganSlot::kHeart,
                                           static_cast<SourceOrganSlot>(index))) {
                integration.is_single_connected_network = false;
                break;
            }
        }
    }
    if (!integration.is_single_connected_network) {
        append_unique(integration.blocking_reason_ids,
                      "source_law.reason.integration.disconnected_network");
    }
    const bool every_slot_is_reaction_covered = std::all_of(
        covered_by_closed_reaction.begin(), covered_by_closed_reaction.end(),
        [](const bool covered) { return covered; });
    integration.has_continuous_global_reaction = integration.all_slots_sublimated &&
                                                 any_closed_system &&
                                                 every_slot_is_reaction_covered;
    if (!integration.has_continuous_global_reaction) {
        append_unique(integration.reaction_blocking_reason_ids,
                      "source_law.reason.integration.global_reaction_incomplete");
    }

    bool integration_conditions_met = false;
    if (integration_definition != nullptr) {
        integration_conditions_met =
            context_has_all_ecology(integration_definition->ecology_conditions, context) &&
            contains(context.satisfied_ritual_ids, integration_definition->integration_ritual_id) &&
            std::isfinite(body.stability) && std::isfinite(body.mutation) &&
            body.stability >= integration_definition->minimum_stability &&
            body.mutation <= integration_definition->maximum_mutation;
        if (!context_has_all_ecology(integration_definition->ecology_conditions, context)) {
            append_unique(integration.blocking_reason_ids,
                          "source_law.reason.integration.ecology");
        }
        if (!contains(context.satisfied_ritual_ids, integration_definition->integration_ritual_id)) {
            append_unique(integration.blocking_reason_ids,
                          "source_law.reason.integration.ritual");
        }
        if (!std::isfinite(body.stability) ||
            body.stability < integration_definition->minimum_stability) {
            append_unique(integration.blocking_reason_ids,
                          "source_law.reason.integration.stability");
        }
        if (!std::isfinite(body.mutation) || body.mutation > integration_definition->maximum_mutation) {
            append_unique(integration.blocking_reason_ids,
                          "source_law.reason.integration.mutation");
        }
    }
    integration.unification_circuit_online = integration.all_slots_sublimated &&
                                             integration.energy_and_refinement_bridge &&
                                             integration.control_and_feedback_bridge &&
                                             integration.environment_and_barrier_bridge &&
                                             integration.load_and_action_bridge &&
                                             integration.is_single_connected_network &&
                                             integration.has_continuous_global_reaction &&
                                             integration_conditions_met;

    const bool any_growing_system = std::any_of(
        evaluation.systems.begin(), evaluation.systems.end(), [](const auto& system) {
            return system.state == SourceLawSystemState::kGrowing;
        });
    if (organs.empty()) {
        integration.stage = SourceBodyStage::kDormant;
    } else if (integration.unification_circuit_online) {
        integration.stage = SourceBodyStage::kInitialComplete;
    } else if (integration.all_slots_sublimated) {
        integration.stage = SourceBodyStage::kEightOrgansSublimated;
    } else if (any_growing_system) {
        integration.stage = SourceBodyStage::kGrowing;
    } else {
        integration.stage = SourceBodyStage::kAwakened;
    }
    evaluation.integration = std::move(integration);

    // The evaluator establishes body capabilities once. The compiler consumes
    // this value snapshot and never re-evaluates organs or reaches into ECS.
    SourceLawBodyCapabilitySnapshot capabilities{
        .body_revision = body.body_revision,
        .active_path_id = body.active_path_id,
        .active_path_is_resonant = evaluation.path.is_resonant,
        .circuit_schedule = evaluation.circuit_schedule.effective_schedule,
        .integration = evaluation.integration,
        .source_throughput = evaluation.derived_source_throughput,
        .stability = std::isfinite(body.stability)
            ? std::clamp(body.stability, 0.0F, 100.0F)
            : 0.0F,
    };
    std::set<SourceBodySystem> closed_body_systems;
    std::set<ElementalReactionStage> available_stages;
    std::set<ElementalPhysiologyAction> available_actions;
    std::vector<SourceLawId> exposed_intrinsic_ids;
    for (const SourceLawSystemReport& system : evaluation.systems) {
        if (system.state != SourceLawSystemState::kClosed &&
            system.state != SourceLawSystemState::kGrowing) {
            continue;
        }
        append_unique(capabilities.active_system_ids, system.system_id);
        closed_body_systems.insert(system.body_system);
        const OrganSystemDefinition* definition = content.find_system(system.system_id);
        if (definition == nullptr) continue;
        for (const SourceLawId& intrinsic_id : definition->intrinsic_operation_ids) {
            append_unique(exposed_intrinsic_ids, intrinsic_id);
        }
        const ElementalReactionDefinition* reaction = content.find_reaction(
            definition->elemental_reaction_id);
        if (reaction != nullptr) {
            append_unique(capabilities.available_product_ids, reaction->product_definition_id);
        }
        const auto collect_actions = [&available_stages, &available_actions](const auto& steps) {
            for (const ElementalReactionStepReport& step : steps) {
                if (!step.is_satisfied) continue;
                for (const SourceLawContributionReference& contributor : step.contributors) {
                    available_stages.insert(contributor.stage);
                    available_actions.insert(contributor.action);
                }
            }
        };
        collect_actions(system.reaction.closure_steps);
        collect_actions(system.reaction.growth_steps);
    }
    const auto primary_supports = [&capabilities, &closed_body_systems, &content](
                                      const SourceLawIntrinsicDefinition& intrinsic) {
        if (!intrinsic.requires_primary_circuit) return true;
        if (!capabilities.circuit_schedule.current_primary_circuit_system_id) return false;
        const OrganSystemDefinition* primary = content.find_system(
            *capabilities.circuit_schedule.current_primary_circuit_system_id);
        return primary != nullptr && closed_body_systems.contains(primary->body_system) &&
               contains(intrinsic.required_closed_systems, primary->body_system);
    };
    for (const auto& [intrinsic_id, intrinsic] : content.intrinsics()) {
        if (!contains(exposed_intrinsic_ids, intrinsic_id) ||
            !std::all_of(intrinsic.required_closed_systems.begin(),
                         intrinsic.required_closed_systems.end(),
                         [&closed_body_systems](const SourceBodySystem system) {
                             return closed_body_systems.contains(system);
                         }) ||
            !std::all_of(intrinsic.required_stages.begin(), intrinsic.required_stages.end(),
                         [&available_stages](const ElementalReactionStage stage) {
                             return available_stages.contains(stage);
                         }) ||
            !std::all_of(intrinsic.required_actions.begin(), intrinsic.required_actions.end(),
                         [&available_actions](const ElementalPhysiologyAction action) {
                             return available_actions.contains(action);
                         }) ||
            !has_all(intrinsic.required_product_tags, capabilities.available_product_ids) ||
            !primary_supports(intrinsic) ||
            (intrinsic.requires_unification_circuit &&
             !capabilities.integration.unification_circuit_online)) {
            continue;
        }
        append_unique(capabilities.available_intrinsic_ids, intrinsic_id);
    }
    evaluation.capability_snapshot = std::move(capabilities);
    return evaluation;
}

SourceLawBodyState SourceLawBodyEvaluator::apply_evaluation(
    SourceLawBodyState body, const SourceLawEvaluation& evaluation) {
    body.source_throughput = evaluation.derived_source_throughput;
    body.mana_max = evaluation.derived_mana_max;
    body.mana_current = std::clamp(body.mana_current, 0, body.mana_max);
    body.mental_load = evaluation.derived_mental_load;
    body.stage = evaluation.integration.stage;
    body.integration = evaluation.integration;
    body.circuit_schedule = evaluation.circuit_schedule.effective_schedule;
    return body;
}

void SourceLawBodyTransitionLogger::log_if_changed(
    std::string_view subject_id,
    const SourceLawEvaluation& previous,
    const SourceLawEvaluation& current) {
    const std::string_view subject = subject_id.empty() ? std::string_view{"<unknown>"} : subject_id;
    for (const SourceLawSystemReport& next : current.systems) {
        const auto previous_system = std::find_if(
            previous.systems.begin(), previous.systems.end(), [&next](const auto& value) {
                return value.system_id == next.system_id;
            });
        const SourceLawSystemState before = previous_system == previous.systems.end()
            ? SourceLawSystemState::kUnavailable
            : previous_system->state;
        if (before != next.state) {
            SNT_LOG_INFO("Source-law system transition subject='%.*s' system='%s' %s -> %s",
                         static_cast<int>(subject.size()), subject.data(), next.system_id.c_str(),
                         source_law_system_state_name(before).data(),
                         source_law_system_state_name(next.state).data());
        }
    }
    if (previous.integration.stage != current.integration.stage ||
        previous.integration.unification_circuit_online !=
            current.integration.unification_circuit_online) {
        SNT_LOG_INFO("Source-law body transition subject='%.*s' stage=%s -> %s integration=%s",
                     static_cast<int>(subject.size()), subject.data(),
                     source_body_stage_name(previous.integration.stage).data(),
                     source_body_stage_name(current.integration.stage).data(),
                     current.integration.unification_circuit_online ? "online" : "offline");
    }
    if (previous.circuit_schedule.effective_schedule.current_primary_circuit_system_id !=
        current.circuit_schedule.effective_schedule.current_primary_circuit_system_id) {
        const SourceLawId before = previous.circuit_schedule.effective_schedule
            .current_primary_circuit_system_id.value_or("<none>");
        const SourceLawId after = current.circuit_schedule.effective_schedule
            .current_primary_circuit_system_id.value_or("<none>");
        SNT_LOG_INFO("Source-law primary circuit transition subject='%.*s' %s -> %s",
                     static_cast<int>(subject.size()), subject.data(), before.c_str(), after.c_str());
    }
}

}  // namespace snt::game::source_law
