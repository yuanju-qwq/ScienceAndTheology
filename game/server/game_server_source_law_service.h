// Dedicated-server source-law runtime boundary.
//
// This module owns only decoded player source-law values and disposable spell
// caches. GameServerPlayerState remains the authority for the ECS component
// and persistence envelope; callers receive explicit transactions rather than
// a mutable body reference.

#pragma once

#include "core/expected.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/source_law/source_law_persistence_codec.h"
#include "game/source_law/source_law_spell_compiler.h"
#include "game/source_law/source_law_transaction_service.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace snt::game::replication {

class GameServerPlayerState;

// World and ritual systems provide value-only facts for a single evaluation.
// Keeping this at the server boundary prevents source-law evaluators from
// retaining World, ECS, script, or transport references.
class IGameServerSourceLawEvaluationContextProvider {
public:
    virtual ~IGameServerSourceLawEvaluationContextProvider() = default;

    [[nodiscard]] virtual snt::core::Expected<source_law::SourceLawEvaluationContext>
    capture_source_law_evaluation_context(const GameAuthenticatedPeer& peer) const = 0;
};

// Quest, replication, and presentation bridges consume committed value events
// here. They cannot mutate the source-law runtime through this interface.
class IGameServerSourceLawEventSink {
public:
    virtual ~IGameServerSourceLawEventSink() = default;

    virtual void on_source_law_event(
        const GameAuthenticatedPeer& peer,
        const source_law::SourceLawTransactionEvent& event) noexcept = 0;
};

struct GameServerSourceLawServiceConfig {
    source_law::SourceLawContentSnapshot content;
    const IGameServerSourceLawEvaluationContextProvider* evaluation_context_provider = nullptr;
    IGameServerSourceLawEventSink* event_sink = nullptr;
};

class GameServerSourceLawService final : public IGameServerPlayerLifecycleParticipant {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerSourceLawService>> create(
        GameServerPlayerState& player_state, GameServerSourceLawServiceConfig config);

    GameServerSourceLawService(const GameServerSourceLawService&) = delete;
    GameServerSourceLawService& operator=(const GameServerSourceLawService&) = delete;

    // The lifecycle is created after this service, so it supplies its dirty
    // checkpoint sink once both authority services exist.
    void set_checkpoint_sink(IGameServerPlayerStateCheckpointSink* checkpoint_sink) noexcept;

    [[nodiscard]] snt::core::Expected<source_law::PlayerSourceLawState> player_state_for_peer(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<source_law::SourceLawEvaluation> evaluation_for_peer(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<source_law::SourceLawBodyCapabilitySnapshot>
    capability_snapshot_for_peer(const GameAuthenticatedPeer& peer) const;

    // Environment-dependent facts can invalidate a compiled spell even when
    // the persisted body revision is unchanged. This refresh only updates the
    // transient evaluation/cache and never writes a player payload by itself.
    [[nodiscard]] snt::core::Expected<source_law::SourceLawEvaluation> refresh_evaluation(
        const GameAuthenticatedPeer& peer);

    [[nodiscard]] snt::core::Expected<source_law::SourceLawTransactionResult> implant(
        const GameAuthenticatedPeer& peer, source_law::SourceLawImplantRequest request);
    [[nodiscard]] snt::core::Expected<source_law::SourceLawTransactionResult> remove_organ(
        const GameAuthenticatedPeer& peer, source_law::SourceLawRemoveOrganRequest request);
    [[nodiscard]] snt::core::Expected<source_law::SourceLawTransactionResult> tune_organ(
        const GameAuthenticatedPeer& peer, source_law::SourceLawTuneOrganRequest request);
    [[nodiscard]] snt::core::Expected<source_law::SourceLawTransactionResult> anchor_path(
        const GameAuthenticatedPeer& peer, source_law::SourceLawAnchorPathRequest request);
    [[nodiscard]] snt::core::Expected<source_law::SourceLawTransactionResult> schedule_circuits(
        const GameAuthenticatedPeer& peer, source_law::SourceLawScheduleRequest request);

    [[nodiscard]] snt::core::Expected<source_law::SourceLawSpellProgramEditResult>
    edit_spell_program(const GameAuthenticatedPeer& peer,
                       source_law::SourceLawSpellProgramEditRequest request);
    [[nodiscard]] snt::core::Expected<source_law::SourceLawSpellProgramEditResult>
    copy_spell_preset(const GameAuthenticatedPeer& peer,
                      source_law::SourceLawSpellProgramId program_id,
                      const source_law::SourceLawId& preset_graph_id,
                      std::string display_name);
    [[nodiscard]] snt::core::Expected<source_law::CompiledSourceLawSpell> compile_spell_program(
        const GameAuthenticatedPeer& peer, source_law::SourceLawSpellProgramId program_id);
    [[nodiscard]] snt::core::Expected<std::optional<source_law::CompiledSourceLawSpell>>
    cached_spell_program(const GameAuthenticatedPeer& peer,
                         source_law::SourceLawSpellProgramId program_id) const;

    [[nodiscard]] size_t active_player_count() const noexcept { return players_.size(); }

    [[nodiscard]] snt::core::Expected<void> on_player_activated(
        const GameAuthenticatedPeer& peer) override;
    void on_player_replaced(const GameAuthenticatedPeer& previous_peer,
                            const GameAuthenticatedPeer& replacement_peer) noexcept override;
    void on_player_deactivated(const GameAuthenticatedPeer& peer,
                               std::string_view reason) noexcept override;

private:
    struct RuntimePlayer {
        source_law::PlayerSourceLawState state;
        source_law::SourceLawEvaluation evaluation;
        std::map<uint64_t, source_law::CompiledSourceLawSpell> compiled_spell_cache;
    };

    GameServerSourceLawService(GameServerPlayerState& player_state,
                               GameServerSourceLawServiceConfig config);

    [[nodiscard]] snt::core::Expected<RuntimePlayer*> runtime_for_peer(
        const GameAuthenticatedPeer& peer);
    [[nodiscard]] snt::core::Expected<const RuntimePlayer*> runtime_for_peer(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<source_law::SourceLawEvaluationContext>
    evaluation_context_for_peer(const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<void> commit_runtime_state(
        const GameAuthenticatedPeer& peer, RuntimePlayer& runtime,
        source_law::PlayerSourceLawState state,
        source_law::SourceLawEvaluation evaluation,
        const source_law::SourceLawEvaluation& previous_evaluation,
        const std::vector<source_law::SourceLawTransactionEvent>& events,
        bool invalidate_all_compiled_spells,
        std::optional<source_law::SourceLawSpellProgramId> invalidate_spell_program);
    void publish_events(const GameAuthenticatedPeer& peer,
                        const std::vector<source_law::SourceLawTransactionEvent>& events) noexcept;

    GameServerPlayerState* player_state_ = nullptr;
    source_law::SourceLawContentSnapshot content_;
    const IGameServerSourceLawEvaluationContextProvider* evaluation_context_provider_ = nullptr;
    IGameServerSourceLawEventSink* event_sink_ = nullptr;
    IGameServerPlayerStateCheckpointSink* checkpoint_sink_ = nullptr;
    source_law::SourceLawPersistenceCodec persistence_codec_;
    std::map<std::string, RuntimePlayer, std::less<>> players_;
};

}  // namespace snt::game::replication
