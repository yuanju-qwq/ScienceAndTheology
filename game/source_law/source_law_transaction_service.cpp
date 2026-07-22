// Source-law body transaction implementation.

#define SNT_LOG_CHANNEL "game.source_law.transaction"
#include "game/source_law/source_law_transaction_service.h"

#include "core/error.h"
#include "core/log.h"

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
    SourceLawBodyTransitionLogger::log_if_changed(subject_id, previous, evaluation);
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
