// Source-law body transaction implementation.

#include "game/source_law/source_law_transaction_service.h"

#include "core/error.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace snt::game::source_law {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

template <typename T>
[[nodiscard]] bool contains(const std::vector<T>& values, const T& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool has_duplicate_ids(const std::vector<SourceLawId>& ids) {
    std::set<SourceLawId> seen;
    for (const SourceLawId& id : ids) {
        if (id.empty() || !seen.insert(id).second) return true;
    }
    return false;
}

[[nodiscard]] bool is_valid_tuning_target(const OrganInstance& organ) {
    return !organ.definition_id.empty() && std::isfinite(organ.contamination) &&
           organ.contamination >= 0.0F && organ.contamination <= 1.0F &&
           std::isfinite(organ.integrity) && organ.integrity > 0.0F &&
           organ.integrity <= 1.0F && !has_duplicate_ids(organ.tuning_tags);
}

[[nodiscard]] bool organ_has_tuning_tag(const OrganDefinition& definition,
                                         const OrganInstance& instance,
                                         const SourceLawId& tag) {
    return contains(definition.bloodline_tags, tag) ||
           contains(definition.ecology_tags, tag) ||
           contains(definition.system_tags, tag) ||
           contains(definition.native_path_tags, tag) ||
           contains(definition.pressure_tags, tag) || contains(instance.tuning_tags, tag);
}

[[nodiscard]] bool tuning_applies_to_organ(const SourceLawTuningDefinition& tuning,
                                            SourceOrganSlot slot,
                                            const OrganDefinition& definition,
                                            const OrganInstance& instance) {
    return contains(tuning.allowed_slots, slot) &&
           std::all_of(tuning.required_organ_tags.begin(), tuning.required_organ_tags.end(),
                       [&definition, &instance](const SourceLawId& tag) {
                           return organ_has_tuning_tag(definition, instance, tag);
                       });
}

[[nodiscard]] const SourceLawSystemReport* find_system_report(
    const SourceLawEvaluation& evaluation, const SourceLawId& id) {
    const auto found = std::find_if(evaluation.systems.begin(), evaluation.systems.end(),
                                    [&id](const auto& report) {
        return report.system_id == id;
    });
    return found == evaluation.systems.end() ? nullptr : &*found;
}

void append_transition_events(const SourceLawEvaluation& before,
                              const SourceLawEvaluation& after,
                              std::string_view subject_id,
                              std::vector<SourceLawTransactionEvent>& events) {
    for (const SourceLawSystemReport& next : after.systems) {
        const SourceLawSystemReport* previous = find_system_report(before, next.system_id);
        const SourceLawSystemState previous_state = previous == nullptr
            ? SourceLawSystemState::kUnavailable
            : previous->state;
        if (previous_state == next.state) continue;
        events.push_back({
            .kind = SourceLawTransactionEventKind::kSystemStateChanged,
            .subject_id = std::string{subject_id},
            .definition_id = next.system_id,
            .previous_system_state = previous_state,
            .current_system_state = next.state,
        });
    }
    if (before.integration.stage != after.integration.stage ||
        before.integration.unification_circuit_online !=
            after.integration.unification_circuit_online) {
        events.push_back({
            .kind = SourceLawTransactionEventKind::kBodyIntegrationStateChanged,
            .subject_id = std::string{subject_id},
            .previous_body_stage = before.integration.stage,
            .current_body_stage = after.integration.stage,
        });
    }
}

[[nodiscard]] SourceLawTransactionResult make_result(
    const SourceLawBodyState& candidate,
    const SourceLawEvaluation& previous,
    const SourceLawEvaluation& evaluation,
    std::string_view subject_id) {
    SourceLawTransactionResult result{
        .body = SourceLawBodyEvaluator::apply_evaluation(candidate, evaluation),
        .evaluation = evaluation,
    };
    append_transition_events(previous, evaluation, subject_id, result.events);
    for (SourceLawTransactionEvent& event : result.events) {
        event.body_revision = result.body.body_revision;
    }
    return result;
}

[[nodiscard]] snt::core::Expected<void> advance_body_revision(SourceLawBodyState& body) {
    if (body.body_revision == std::numeric_limits<uint64_t>::max()) {
        return invalid_state("Source-law body revision is exhausted");
    }
    ++body.body_revision;
    return {};
}

}  // namespace

snt::core::Expected<SourceLawTransactionResult> SourceLawTransactionService::implant(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyState& body,
    const SourceLawImplantRequest& request,
    const SourceLawEvaluationContext& context) {
    if (!is_valid_source_organ_slot(request.slot) || request.organ_definition_id.empty() ||
        request.source_reserve_cost < 0 || has_duplicate_ids(request.tuning_tags)) {
        return invalid_argument("Source-law implant request is invalid");
    }
    const OrganDefinition* definition = content.find_organ(request.organ_definition_id);
    if (definition == nullptr || definition->slot != request.slot) {
        return invalid_argument("Source-law implant organ definition does not match the requested slot");
    }
    const size_t slot_index = static_cast<size_t>(request.slot);
    if (body.organs[slot_index]) {
        return invalid_state("Source-law implant target slot is already occupied");
    }
    if (body.source_reserve_current < request.source_reserve_cost) {
        return invalid_state("Source-law implant has insufficient source reserve");
    }
    const SourceLawEvaluation before = SourceLawBodyEvaluator::evaluate(content, body, context);
    SourceLawBodyState candidate = body;
    candidate.source_reserve_current -= request.source_reserve_cost;
    candidate.organs[slot_index] = OrganInstance{
        .definition_id = definition->id,
        .growth_level = request.initial_growth_level,
        .quality_id = request.quality_id,
        .contamination = 0.0F,
        .integrity = 1.0F,
        .tuning_tags = request.tuning_tags,
    };
    if (auto result = advance_body_revision(candidate); !result) return result.error();
    const SourceLawEvaluation after = SourceLawBodyEvaluator::evaluate(content, candidate, context);
    SourceLawTransactionResult result = make_result(candidate, before, after,
                                                     request.diagnostic_subject_id);
    result.events.insert(result.events.begin(), {
        .kind = SourceLawTransactionEventKind::kOrganImplanted,
        .subject_id = std::string{request.diagnostic_subject_id},
        .definition_id = definition->id,
        .slot = request.slot,
        .body_revision = result.body.body_revision,
    });
    return result;
}

snt::core::Expected<SourceLawTransactionResult> SourceLawTransactionService::remove_organ(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyState& body,
    const SourceLawRemoveOrganRequest& request,
    const SourceLawEvaluationContext& context) {
    if (!is_valid_source_organ_slot(request.slot)) {
        return invalid_argument("Source-law organ removal slot is invalid");
    }
    const size_t slot_index = static_cast<size_t>(request.slot);
    if (!body.organs[slot_index]) {
        return invalid_state("Source-law organ removal target slot is empty");
    }
    const SourceLawId removed_definition_id = body.organs[slot_index]->definition_id;
    const SourceLawEvaluation before = SourceLawBodyEvaluator::evaluate(content, body, context);
    SourceLawBodyState candidate = body;
    candidate.organs[slot_index].reset();
    if (auto result = advance_body_revision(candidate); !result) return result.error();
    const SourceLawEvaluation after = SourceLawBodyEvaluator::evaluate(content, candidate, context);
    SourceLawTransactionResult result = make_result(candidate, before, after,
                                                     request.diagnostic_subject_id);
    result.events.insert(result.events.begin(), {
        .kind = SourceLawTransactionEventKind::kOrganRemoved,
        .subject_id = std::string{request.diagnostic_subject_id},
        .definition_id = removed_definition_id,
        .slot = request.slot,
        .body_revision = result.body.body_revision,
    });
    return result;
}

snt::core::Expected<SourceLawTransactionResult> SourceLawTransactionService::tune_organ(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyState& body,
    const SourceLawTuneOrganRequest& request,
    const SourceLawEvaluationContext& context) {
    if (!is_valid_source_organ_slot(request.slot) || request.tuning_definition_id.empty()) {
        return invalid_argument("Source-law organ tuning request is invalid");
    }
    if (!std::isfinite(body.stability) || body.stability < 0.0F || body.stability > 100.0F ||
        !std::isfinite(body.mutation) || body.mutation < 0.0F || body.mutation > 100.0F) {
        return invalid_state("Source-law organ tuning requires a valid body risk state");
    }
    const SourceLawTuningDefinition* tuning = content.find_tuning(request.tuning_definition_id);
    if (tuning == nullptr) {
        return invalid_argument("Source-law organ tuning references an unknown definition");
    }
    const size_t slot_index = static_cast<size_t>(request.slot);
    if (!body.organs[slot_index]) {
        return invalid_state("Source-law organ tuning target slot is empty");
    }
    const OrganInstance& target = *body.organs[slot_index];
    const OrganDefinition* definition = content.find_organ(target.definition_id);
    if (definition == nullptr || definition->slot != request.slot ||
        !is_valid_tuning_target(target)) {
        return invalid_state("Source-law organ tuning target is not a usable installed organ");
    }
    if (!tuning_applies_to_organ(*tuning, request.slot, *definition, target)) {
        return invalid_state("Source-law tuning definition is incompatible with the target organ");
    }
    if (body.mutation > tuning->maximum_mutation_before) {
        return invalid_state("Source-law tuning cannot purify this severe mutation state");
    }
    if (body.source_reserve_current < tuning->source_reserve_cost) {
        return invalid_state("Source-law organ tuning has insufficient source reserve");
    }

    const SourceLawEvaluation before = SourceLawBodyEvaluator::evaluate(content, body, context);
    SourceLawBodyState candidate = body;
    OrganInstance& tuned = *candidate.organs[slot_index];
    tuned.tuning_tags.erase(
        std::remove_if(tuned.tuning_tags.begin(), tuned.tuning_tags.end(),
                       [&tuning](const SourceLawId& tag) {
                           return contains(tuning->removed_tuning_tags, tag);
                       }),
        tuned.tuning_tags.end());
    for (const SourceLawId& tag : tuning->added_tuning_tags) {
        if (!contains(tuned.tuning_tags, tag)) tuned.tuning_tags.push_back(tag);
    }
    tuned.contamination = std::max(0.0F, tuned.contamination - tuning->contamination_reduction);
    candidate.mutation = std::max(0.0F, candidate.mutation - tuning->mutation_reduction);
    candidate.stability = std::clamp(candidate.stability + tuning->stability_delta, 0.0F, 100.0F);
    if (tuned == target && candidate.mutation == body.mutation &&
        candidate.stability == body.stability) {
        return invalid_state("Source-law organ tuning has no remaining effect on the target");
    }
    candidate.source_reserve_current -= tuning->source_reserve_cost;
    if (auto result = advance_body_revision(candidate); !result) return result.error();

    // Re-evaluate every affected reaction and risk-dependent integration after
    // the complete candidate is formed. A tuning may intentionally change a
    // system's availability, so diagnostics are returned as value events.
    const SourceLawEvaluation after = SourceLawBodyEvaluator::evaluate(content, candidate, context);
    SourceLawTransactionResult result = make_result(candidate, before, after,
                                                     request.diagnostic_subject_id);
    const OrganInstance& committed = *result.body.organs[slot_index];
    result.events.insert(result.events.begin(), {
        .kind = SourceLawTransactionEventKind::kOrganTuned,
        .subject_id = std::string{request.diagnostic_subject_id},
        .definition_id = tuning->id,
        .slot = request.slot,
        .body_revision = result.body.body_revision,
        .contamination_delta = committed.contamination - target.contamination,
        .mutation_delta = result.body.mutation - body.mutation,
        .stability_delta = result.body.stability - body.stability,
    });
    return result;
}

snt::core::Expected<SourceLawTransactionResult> SourceLawTransactionService::anchor_path(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyState& body,
    const SourceLawAnchorPathRequest& request,
    const SourceLawEvaluationContext& context) {
    if (request.path_id.empty() || content.find_path(request.path_id) == nullptr) {
        return invalid_argument("Source-law path anchor references an unknown path");
    }
    const SourceLawEvaluation before = SourceLawBodyEvaluator::evaluate(content, body, context);
    SourceLawBodyState candidate = body;
    candidate.active_path_id = request.path_id;
    if (auto result = advance_body_revision(candidate); !result) return result.error();
    const SourceLawEvaluation after = SourceLawBodyEvaluator::evaluate(content, candidate, context);
    SourceLawTransactionResult result = make_result(candidate, before, after,
                                                     request.diagnostic_subject_id);
    result.events.insert(result.events.begin(), {
        .kind = SourceLawTransactionEventKind::kPathAnchored,
        .subject_id = std::string{request.diagnostic_subject_id},
        .definition_id = request.path_id,
        .body_revision = result.body.body_revision,
    });
    return result;
}

snt::core::Expected<SourceLawTransactionResult> SourceLawTransactionService::schedule_circuits(
    const SourceLawContentSnapshot& content,
    const SourceLawBodyState& body,
    const SourceLawScheduleRequest& request,
    const SourceLawEvaluationContext& context) {
    if (has_duplicate_ids(request.coordinating_system_ids) ||
        (request.primary_system_id && request.primary_system_id->empty()) ||
        !std::isfinite(request.reallocation_cooldown_seconds) ||
        request.reallocation_cooldown_seconds < 0.0F) {
        return invalid_argument("Source-law circuit schedule request is invalid");
    }
    if (request.respect_existing_cooldown &&
        body.circuit_schedule.primary_circuit_reallocation_cooldown_seconds > 0.0F) {
        return invalid_state("Source-law primary circuit is still on reallocation cooldown");
    }
    const SourceLawEvaluation before = SourceLawBodyEvaluator::evaluate(content, body, context);
    SourceLawBodyState candidate = body;
    candidate.circuit_schedule = {
        .current_primary_circuit_system_id = request.primary_system_id,
        .coordinating_circuit_system_ids = request.coordinating_system_ids,
        .primary_circuit_reallocation_cooldown_seconds = request.reallocation_cooldown_seconds,
    };
    if (auto result = advance_body_revision(candidate); !result) return result.error();
    const SourceLawEvaluation after = SourceLawBodyEvaluator::evaluate(content, candidate, context);
    if (!after.circuit_schedule.primary_circuit_is_valid ||
        !after.circuit_schedule.rejected_circuit_system_ids.empty() ||
        after.circuit_schedule.effective_schedule.current_primary_circuit_system_id !=
            request.primary_system_id ||
        after.circuit_schedule.effective_schedule.coordinating_circuit_system_ids !=
            request.coordinating_system_ids) {
        return invalid_state("Source-law circuit schedule references a system that is not closed");
    }
    SourceLawTransactionResult result = make_result(candidate, before, after,
                                                     request.diagnostic_subject_id);
    result.events.insert(result.events.begin(), {
        .kind = SourceLawTransactionEventKind::kCircuitScheduled,
        .subject_id = std::string{request.diagnostic_subject_id},
        .definition_id = request.primary_system_id.value_or(""),
        .body_revision = result.body.body_revision,
    });
    return result;
}

void SourceLawTransactionService::publish_events(const SourceLawTransactionResult& result,
                                                  ISourceLawEventSink* sink) noexcept {
    if (sink == nullptr) return;
    for (const SourceLawTransactionEvent& event : result.events) {
        sink->on_source_law_transaction_event(event);
    }
}

}  // namespace snt::game::source_law
