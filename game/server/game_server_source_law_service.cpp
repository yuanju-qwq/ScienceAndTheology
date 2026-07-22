// Dedicated-server source-law runtime implementation.

#define SNT_LOG_CHANNEL "game.server_source_law"
#include "game/server/game_server_source_law_service.h"

#include "core/error.h"
#include "core/log.h"
#include "game/server/game_server_player_state.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] const source_law::PlayerSourceLawSpellProgram* find_spell_program(
    const source_law::PlayerSourceLawState& state,
    source_law::SourceLawSpellProgramId program_id) {
    const auto found = std::find_if(
        state.personal_spell_programs.begin(), state.personal_spell_programs.end(),
        [program_id](const source_law::PlayerSourceLawSpellProgram& program) {
            return program.program_id == program_id;
        });
    return found == state.personal_spell_programs.end() ? nullptr : &*found;
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerSourceLawService>>
GameServerSourceLawService::create(GameServerPlayerState& player_state,
                                   GameServerSourceLawServiceConfig config) {
    if (config.content.revision() == 0) {
        return invalid_argument("Game server source-law service requires a content revision");
    }
    return std::unique_ptr<GameServerSourceLawService>(
        new GameServerSourceLawService(player_state, std::move(config)));
}

GameServerSourceLawService::GameServerSourceLawService(
    GameServerPlayerState& player_state, GameServerSourceLawServiceConfig config)
    : player_state_(&player_state), content_(std::move(config.content)),
      evaluation_context_provider_(config.evaluation_context_provider),
      event_sink_(config.event_sink) {}

void GameServerSourceLawService::set_checkpoint_sink(
    IGameServerPlayerStateCheckpointSink* checkpoint_sink) noexcept {
    checkpoint_sink_ = checkpoint_sink;
}

snt::core::Expected<GameServerSourceLawService::RuntimePlayer*>
GameServerSourceLawService::runtime_for_peer(const GameAuthenticatedPeer& peer) {
    if (player_state_ == nullptr) return invalid_state("Game server source-law service has no player state");
    if (auto snapshot = player_state_->snapshot_for_peer(peer); !snapshot) {
        return snapshot.error();
    }
    const auto player = players_.find(peer.identity.account_id);
    if (player == players_.end()) {
        return invalid_state("Game server source-law player runtime is not active");
    }
    return &player->second;
}

snt::core::Expected<const GameServerSourceLawService::RuntimePlayer*>
GameServerSourceLawService::runtime_for_peer(const GameAuthenticatedPeer& peer) const {
    if (player_state_ == nullptr) return invalid_state("Game server source-law service has no player state");
    if (auto snapshot = player_state_->snapshot_for_peer(peer); !snapshot) {
        return snapshot.error();
    }
    const auto player = players_.find(peer.identity.account_id);
    if (player == players_.end()) {
        return invalid_state("Game server source-law player runtime is not active");
    }
    return &player->second;
}

snt::core::Expected<source_law::SourceLawEvaluationContext>
GameServerSourceLawService::evaluation_context_for_peer(const GameAuthenticatedPeer& peer) const {
    if (evaluation_context_provider_ == nullptr) return source_law::SourceLawEvaluationContext{};
    auto context = evaluation_context_provider_->capture_source_law_evaluation_context(peer);
    if (!context) {
        auto error = context.error();
        error.with_context("GameServerSourceLawService::evaluation_context_for_peer");
        return error;
    }
    return std::move(*context);
}

snt::core::Expected<source_law::PlayerSourceLawState>
GameServerSourceLawService::player_state_for_peer(const GameAuthenticatedPeer& peer) const {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    return (*runtime)->state;
}

snt::core::Expected<source_law::SourceLawEvaluation>
GameServerSourceLawService::evaluation_for_peer(const GameAuthenticatedPeer& peer) const {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    return (*runtime)->evaluation;
}

snt::core::Expected<source_law::SourceLawBodyCapabilitySnapshot>
GameServerSourceLawService::capability_snapshot_for_peer(const GameAuthenticatedPeer& peer) const {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    return (*runtime)->evaluation.capability_snapshot;
}

snt::core::Expected<source_law::SourceLawEvaluation>
GameServerSourceLawService::refresh_evaluation(const GameAuthenticatedPeer& peer) {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    auto context = evaluation_context_for_peer(peer);
    if (!context) return context.error();
    source_law::SourceLawEvaluation evaluation = source_law::SourceLawBodyEvaluator::evaluate(
        content_, (*runtime)->state.body, *context);
    if (evaluation.capability_snapshot != (*runtime)->evaluation.capability_snapshot) {
        (*runtime)->compiled_spell_cache.clear();
    }
    (*runtime)->evaluation = std::move(evaluation);
    return (*runtime)->evaluation;
}

snt::core::Expected<void> GameServerSourceLawService::commit_runtime_state(
    const GameAuthenticatedPeer& peer, RuntimePlayer& runtime,
    source_law::PlayerSourceLawState state, source_law::SourceLawEvaluation evaluation,
    const source_law::SourceLawEvaluation& previous_evaluation,
    const std::vector<source_law::SourceLawTransactionEvent>& events,
    bool invalidate_all_compiled_spells,
    std::optional<source_law::SourceLawSpellProgramId> invalidate_spell_program) {
    auto encoded = persistence_codec_.encode(state);
    if (!encoded) {
        auto error = encoded.error();
        error.with_context("GameServerSourceLawService::commit_runtime_state(encode)");
        return error;
    }
    if (checkpoint_sink_ != nullptr) {
        if (auto result = checkpoint_sink_->mark_player_state_dirty(peer); !result) {
            auto error = result.error();
            error.with_context("GameServerSourceLawService::commit_runtime_state(checkpoint)");
            return error;
        }
    }
    if (auto result = player_state_->set_authoritative_organ_state(peer, std::move(*encoded));
        !result) {
        auto error = result.error();
        error.with_context("GameServerSourceLawService::commit_runtime_state(player state)");
        return error;
    }
    runtime.state = std::move(state);
    runtime.evaluation = std::move(evaluation);
    if (invalidate_all_compiled_spells) {
        runtime.compiled_spell_cache.clear();
    } else if (invalidate_spell_program.has_value()) {
        runtime.compiled_spell_cache.erase(invalidate_spell_program->value);
    }
    source_law::SourceLawBodyTransitionLogger::log_if_changed(
        peer.identity.account_id, previous_evaluation, runtime.evaluation);
    publish_events(peer, events);
    return {};
}

void GameServerSourceLawService::publish_events(
    const GameAuthenticatedPeer& peer,
    const std::vector<source_law::SourceLawTransactionEvent>& events) noexcept {
    if (event_sink_ == nullptr) return;
    for (const source_law::SourceLawTransactionEvent& event : events) {
        event_sink_->on_source_law_event(peer, event);
    }
}

snt::core::Expected<void> GameServerSourceLawService::on_player_activated(
    const GameAuthenticatedPeer& peer) {
    if (player_state_ == nullptr) return invalid_state("Game server source-law service has no player state");
    if (players_.contains(peer.identity.account_id)) {
        return invalid_state("Game server source-law player runtime is already active");
    }
    auto persisted = player_state_->organ_state_for_peer(peer);
    if (!persisted) return persisted.error();
    auto state = persistence_codec_.decode(*persisted);
    if (!state) {
        auto error = state.error();
        error.with_context("GameServerSourceLawService::on_player_activated(decode)");
        return error;
    }
    auto context = evaluation_context_for_peer(peer);
    if (!context) return context.error();
    source_law::SourceLawEvaluation evaluation = source_law::SourceLawBodyEvaluator::evaluate(
        content_, state->body, *context);
    state->body = source_law::SourceLawBodyEvaluator::apply_evaluation(state->body, evaluation);
    auto encoded = persistence_codec_.encode(*state);
    if (!encoded) {
        auto error = encoded.error();
        error.with_context("GameServerSourceLawService::on_player_activated(encode)");
        return error;
    }
    if (auto result = player_state_->set_authoritative_organ_state(peer, std::move(*encoded));
        !result) {
        auto error = result.error();
        error.with_context("GameServerSourceLawService::on_player_activated(player state)");
        return error;
    }
    players_.emplace(peer.identity.account_id, RuntimePlayer{
        .state = std::move(*state),
        .evaluation = std::move(evaluation),
    });
    SNT_LOG_INFO("Activated source-law runtime for account '%s' content_revision=%llu body_revision=%llu",
                 peer.identity.account_id.c_str(),
                 static_cast<unsigned long long>(content_.revision()),
                 static_cast<unsigned long long>(players_.at(peer.identity.account_id)
                                                      .state.body.body_revision));
    return {};
}

void GameServerSourceLawService::on_player_replaced(
    const GameAuthenticatedPeer& previous_peer,
    const GameAuthenticatedPeer& replacement_peer) noexcept {
    if (previous_peer.identity.account_id != replacement_peer.identity.account_id) return;
    // Runtime values are keyed by stable account id, so an in-process peer
    // takeover keeps its body and disposable spell cache intact.
}

void GameServerSourceLawService::on_player_deactivated(const GameAuthenticatedPeer& peer,
                                                        std::string_view reason) noexcept {
    const size_t removed = players_.erase(peer.identity.account_id);
    if (removed != 0) {
        SNT_LOG_INFO("Released source-law runtime for account '%s' (%.*s)",
                     peer.identity.account_id.c_str(), static_cast<int>(reason.size()), reason.data());
    }
}

snt::core::Expected<source_law::SourceLawTransactionResult>
GameServerSourceLawService::implant(const GameAuthenticatedPeer& peer,
                                    source_law::SourceLawImplantRequest request) {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    auto context = evaluation_context_for_peer(peer);
    if (!context) return context.error();
    const source_law::SourceLawEvaluation previous = source_law::SourceLawBodyEvaluator::evaluate(
        content_, (*runtime)->state.body, *context);
    request.diagnostic_subject_id = peer.identity.account_id;
    auto transaction = source_law::SourceLawTransactionService::implant(
        content_, (*runtime)->state.body, request, *context);
    if (!transaction) return transaction.error();
    source_law::PlayerSourceLawState state = (*runtime)->state;
    state.body = transaction->body;
    if (auto result = commit_runtime_state(peer, **runtime, std::move(state),
                                           transaction->evaluation, previous,
                                           transaction->events, true, std::nullopt);
        !result) {
        return result.error();
    }
    return std::move(*transaction);
}

snt::core::Expected<source_law::SourceLawTransactionResult>
GameServerSourceLawService::remove_organ(const GameAuthenticatedPeer& peer,
                                         source_law::SourceLawRemoveOrganRequest request) {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    auto context = evaluation_context_for_peer(peer);
    if (!context) return context.error();
    const source_law::SourceLawEvaluation previous = source_law::SourceLawBodyEvaluator::evaluate(
        content_, (*runtime)->state.body, *context);
    request.diagnostic_subject_id = peer.identity.account_id;
    auto transaction = source_law::SourceLawTransactionService::remove_organ(
        content_, (*runtime)->state.body, request, *context);
    if (!transaction) return transaction.error();
    source_law::PlayerSourceLawState state = (*runtime)->state;
    state.body = transaction->body;
    if (auto result = commit_runtime_state(peer, **runtime, std::move(state),
                                           transaction->evaluation, previous,
                                           transaction->events, true, std::nullopt);
        !result) {
        return result.error();
    }
    return std::move(*transaction);
}

snt::core::Expected<source_law::SourceLawTransactionResult>
GameServerSourceLawService::tune_organ(const GameAuthenticatedPeer& peer,
                                       source_law::SourceLawTuneOrganRequest request) {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    auto context = evaluation_context_for_peer(peer);
    if (!context) return context.error();
    const source_law::SourceLawEvaluation previous = source_law::SourceLawBodyEvaluator::evaluate(
        content_, (*runtime)->state.body, *context);
    request.diagnostic_subject_id = peer.identity.account_id;
    auto transaction = source_law::SourceLawTransactionService::tune_organ(
        content_, (*runtime)->state.body, request, *context);
    if (!transaction) return transaction.error();
    source_law::PlayerSourceLawState state = (*runtime)->state;
    state.body = transaction->body;
    if (auto result = commit_runtime_state(peer, **runtime, std::move(state),
                                           transaction->evaluation, previous,
                                           transaction->events, true, std::nullopt);
        !result) {
        return result.error();
    }
    const auto tuned_event = std::find_if(
        transaction->events.begin(), transaction->events.end(), [](const auto& event) {
            return event.kind == source_law::SourceLawTransactionEventKind::kOrganTuned;
        });
    if (tuned_event != transaction->events.end()) {
        const std::string_view slot_name = source_law::source_organ_slot_name(tuned_event->slot);
        SNT_LOG_INFO(
            "Committed source-law organ tuning account='%s' tuning='%s' slot=%.*s body_revision=%llu contamination_delta=%.3f mutation_delta=%.3f stability_delta=%.3f",
            peer.identity.account_id.c_str(), tuned_event->definition_id.c_str(),
            static_cast<int>(slot_name.size()), slot_name.data(),
            static_cast<unsigned long long>(tuned_event->body_revision),
            static_cast<double>(tuned_event->contamination_delta),
            static_cast<double>(tuned_event->mutation_delta),
            static_cast<double>(tuned_event->stability_delta));
    }
    return std::move(*transaction);
}

snt::core::Expected<source_law::SourceLawTransactionResult>
GameServerSourceLawService::anchor_path(const GameAuthenticatedPeer& peer,
                                        source_law::SourceLawAnchorPathRequest request) {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    auto context = evaluation_context_for_peer(peer);
    if (!context) return context.error();
    const source_law::SourceLawEvaluation previous = source_law::SourceLawBodyEvaluator::evaluate(
        content_, (*runtime)->state.body, *context);
    request.diagnostic_subject_id = peer.identity.account_id;
    auto transaction = source_law::SourceLawTransactionService::anchor_path(
        content_, (*runtime)->state.body, request, *context);
    if (!transaction) return transaction.error();
    source_law::PlayerSourceLawState state = (*runtime)->state;
    state.body = transaction->body;
    if (auto result = commit_runtime_state(peer, **runtime, std::move(state),
                                           transaction->evaluation, previous,
                                           transaction->events, true, std::nullopt);
        !result) {
        return result.error();
    }
    return std::move(*transaction);
}

snt::core::Expected<source_law::SourceLawTransactionResult>
GameServerSourceLawService::schedule_circuits(const GameAuthenticatedPeer& peer,
                                              source_law::SourceLawScheduleRequest request) {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    auto context = evaluation_context_for_peer(peer);
    if (!context) return context.error();
    const source_law::SourceLawEvaluation previous = source_law::SourceLawBodyEvaluator::evaluate(
        content_, (*runtime)->state.body, *context);
    request.diagnostic_subject_id = peer.identity.account_id;
    auto transaction = source_law::SourceLawTransactionService::schedule_circuits(
        content_, (*runtime)->state.body, request, *context);
    if (!transaction) return transaction.error();
    source_law::PlayerSourceLawState state = (*runtime)->state;
    state.body = transaction->body;
    if (auto result = commit_runtime_state(peer, **runtime, std::move(state),
                                           transaction->evaluation, previous,
                                           transaction->events, true, std::nullopt);
        !result) {
        return result.error();
    }
    return std::move(*transaction);
}

snt::core::Expected<source_law::SourceLawSpellProgramEditResult>
GameServerSourceLawService::edit_spell_program(
    const GameAuthenticatedPeer& peer, source_law::SourceLawSpellProgramEditRequest request) {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    auto edited = source_law::SourceLawSpellProgramService::edit(
        content_, (*runtime)->state, std::move(request));
    if (!edited) return edited.error();
    source_law::SourceLawSpellProgramEditResult result = *edited;
    const source_law::SourceLawTransactionEvent event{
        .kind = source_law::SourceLawTransactionEventKind::kSpellGraphChanged,
        .subject_id = peer.identity.account_id,
        .spell_program_id = result.program.program_id,
        .source_revision = result.program.source_revision,
        .body_revision = result.state.body.body_revision,
    };
    if (auto commit = commit_runtime_state(peer, **runtime, std::move(edited->state),
                                           (*runtime)->evaluation, (*runtime)->evaluation,
                                           {event}, false, result.program.program_id);
        !commit) {
        return commit.error();
    }
    SNT_LOG_INFO("Committed source-law personal spell account='%s' program=%llu source_revision=%u",
                 peer.identity.account_id.c_str(),
                 static_cast<unsigned long long>(result.program.program_id.value),
                 result.program.source_revision);
    return result;
}

snt::core::Expected<source_law::SourceLawSpellProgramEditResult>
GameServerSourceLawService::copy_spell_preset(
    const GameAuthenticatedPeer& peer, source_law::SourceLawSpellProgramId program_id,
    const source_law::SourceLawId& preset_graph_id, std::string display_name) {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    auto edited = source_law::SourceLawSpellProgramService::copy_preset(
        content_, (*runtime)->state, program_id, preset_graph_id, std::move(display_name));
    if (!edited) return edited.error();
    source_law::SourceLawSpellProgramEditResult result = *edited;
    const source_law::SourceLawTransactionEvent event{
        .kind = source_law::SourceLawTransactionEventKind::kSpellGraphChanged,
        .subject_id = peer.identity.account_id,
        .spell_program_id = result.program.program_id,
        .source_revision = result.program.source_revision,
        .body_revision = result.state.body.body_revision,
    };
    if (auto commit = commit_runtime_state(peer, **runtime, std::move(edited->state),
                                           (*runtime)->evaluation, (*runtime)->evaluation,
                                           {event}, false, result.program.program_id);
        !commit) {
        return commit.error();
    }
    SNT_LOG_INFO("Committed source-law personal spell account='%s' program=%llu source_revision=%u",
                 peer.identity.account_id.c_str(),
                 static_cast<unsigned long long>(result.program.program_id.value),
                 result.program.source_revision);
    return result;
}

snt::core::Expected<source_law::CompiledSourceLawSpell>
GameServerSourceLawService::compile_spell_program(
    const GameAuthenticatedPeer& peer, source_law::SourceLawSpellProgramId program_id) {
    auto evaluation = refresh_evaluation(peer);
    if (!evaluation) return evaluation.error();
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    const source_law::PlayerSourceLawSpellProgram* program = find_spell_program(
        (*runtime)->state, program_id);
    if (program == nullptr) {
        return invalid_argument("Game server source-law spell program is not owned by this player");
    }
    source_law::CompiledSourceLawSpell compiled = source_law::SourceLawSpellCompiler::compile_program(
        content_, evaluation->capability_snapshot, *program);
    (*runtime)->compiled_spell_cache.insert_or_assign(program_id.value, compiled);
    publish_events(peer, {{
        .kind = source_law::SourceLawTransactionEventKind::kSpellCompilationChanged,
        .subject_id = peer.identity.account_id,
        .spell_program_id = program_id,
        .source_revision = program->source_revision,
        .body_revision = (*runtime)->state.body.body_revision,
        .is_compilable = compiled.report.is_compilable,
        .blocking_reason_ids = compiled.report.blocking_reason_ids,
    }});
    return compiled;
}

snt::core::Expected<std::optional<source_law::CompiledSourceLawSpell>>
GameServerSourceLawService::cached_spell_program(
    const GameAuthenticatedPeer& peer, source_law::SourceLawSpellProgramId program_id) const {
    auto runtime = runtime_for_peer(peer);
    if (!runtime) return runtime.error();
    const source_law::PlayerSourceLawSpellProgram* program = find_spell_program(
        (*runtime)->state, program_id);
    if (program == nullptr) return std::optional<source_law::CompiledSourceLawSpell>{};
    const auto cached = (*runtime)->compiled_spell_cache.find(program_id.value);
    if (cached == (*runtime)->compiled_spell_cache.end() ||
        !source_law::SourceLawSpellCompiler::is_current(
            cached->second, *program, (*runtime)->state.body.body_revision)) {
        return std::optional<source_law::CompiledSourceLawSpell>{};
    }
    return std::optional<source_law::CompiledSourceLawSpell>{cached->second};
}

}  // namespace snt::game::replication
