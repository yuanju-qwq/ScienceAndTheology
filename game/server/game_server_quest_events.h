// Dedicated-server bridge from committed gameplay events to quest values.
//
// Ownership: the server session owns this bridge. It receives only committed
// interaction and machine values, resolves stable account ids through the
// authoritative player-state service, and never exposes World ownership to
// QuestRegistry or reward consumers.

#pragma once

#include "core/expected.h"
#include "game/client/machine_tick_system.h"
#include "game/quest/quest_registry.h"
#include "game/server/game_server_player_interaction.h"

#include <cstdint>
#include <string_view>

namespace snt::game::replication {

class GameServerPlayerState;
class IGameServerPlayerStateCheckpointSink;

class GameServerQuestEventService final
    : public IGameServerPlayerInteractionEventSink,
      public IMachineTickEventSink,
      public IQuestRewardSink {
public:
    explicit GameServerQuestEventService(QuestRegistry& quests) noexcept;

    GameServerQuestEventService(const GameServerQuestEventService&) = delete;
    GameServerQuestEventService& operator=(const GameServerQuestEventService&) = delete;

    // Binding happens once server player state and its persistence lifecycle
    // are constructed. Before that point no authenticated gameplay event can
    // be dispatched because the server has not accepted any peer.
    void bind_player_state(
        GameServerPlayerState& player_state,
        IGameServerPlayerStateCheckpointSink* checkpoint_sink = nullptr) noexcept;
    void unbind_player_state() noexcept;

    void on_player_interaction(const GameServerPlayerInteractionEvent& event) override;
    void on_machine_tick_event(const MachineTickEvent& event) override;
    [[nodiscard]] snt::core::Expected<void> grant_item_rewards(
        std::string_view player_id, std::string_view quest_id,
        std::span<const QuestItemReward> rewards) override;

private:
    [[nodiscard]] snt::core::Expected<void> update_inventory_objectives(
        std::string_view account_id, uint64_t tick_index);
    void record_progress(std::string_view account_id, QuestObjectiveKind kind,
                         std::string_view target_id, int32_t amount,
                         uint64_t tick_index) noexcept;
    void record_error(uint64_t tick_index, std::string_view source,
                      const snt::core::Error& error) noexcept;

    QuestRegistry* quests_ = nullptr;
    GameServerPlayerState* player_state_ = nullptr;
    IGameServerPlayerStateCheckpointSink* checkpoint_sink_ = nullptr;
    uint64_t last_error_tick_ = 0;
    uint32_t suppressed_errors_ = 0;
    bool has_last_error_tick_ = false;
};

}  // namespace snt::game::replication
