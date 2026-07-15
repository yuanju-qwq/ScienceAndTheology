// Dedicated-server authenticated-player persistence lifecycle.
//
// This composition module keeps per-player task load/save policy outside the
// transport handler and command sink. It owns the current universe save root,
// while QuestRegistry retains gameplay values and GameSaveManager owns the
// strict file format. All calls occur on the simulation main thread.

#pragma once

#include "game/network/game_replication_services.h"
#include "game/quest/quest_registry.h"
#include "game/world/save/quest_progress_persistence.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace snt::game::replication {

class GameServerPlayerLifecycle final : public IGamePlayerSessionLifecycle {
public:
    // An interval of zero disables periodic autosave; disconnect and
    // controlled-shutdown saves remain mandatory in either case.
    GameServerPlayerLifecycle(QuestRegistry& quests, std::string universe_save_dir,
                              uint64_t autosave_interval_ticks = 0);

    GameServerPlayerLifecycle(const GameServerPlayerLifecycle&) = delete;
    GameServerPlayerLifecycle& operator=(const GameServerPlayerLifecycle&) = delete;

    [[nodiscard]] snt::core::Expected<void> on_peer_authenticated(
        const GameAuthenticatedPeer& peer,
        const snt::network::ReplicationTickContext& context) override;
    [[nodiscard]] snt::core::Expected<void> on_peer_replaced(
        const GameAuthenticatedPeer& previous_peer,
        const GameAuthenticatedPeer& replacement_peer,
        const snt::network::ReplicationTickContext& context) override;
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;

    // A controlled server shutdown persists every account resident in this
    // process, including a player whose previous disconnect save failed. This
    // remains a lifecycle operation, never a fixed-tick task.
    [[nodiscard]] snt::core::Expected<void> flush_all();
    // Called after the simulation fixed-tick barrier. It writes only resident
    // accounts whose QuestRegistry value revision changed since their last
    // successful save, and never performs work more often than configured.
    [[nodiscard]] snt::core::Expected<void> flush_due(uint64_t tick_index);
    void shutdown() noexcept;

    [[nodiscard]] size_t active_player_count() const noexcept { return active_peers_.size(); }
    [[nodiscard]] const std::string& universe_save_dir() const noexcept {
        return persistence_.save_dir();
    }

private:
    [[nodiscard]] snt::core::Expected<void> validate_peer(
        const GameAuthenticatedPeer& peer) const;
    [[nodiscard]] snt::core::Expected<void> save_account(std::string_view account_id);

    QuestRegistry* quests_ = nullptr;
    GameSaveQuestProgressPersistence persistence_;
    std::map<snt::network::PeerId, std::string> active_peers_;
    std::map<std::string, snt::network::PeerId, std::less<>> active_accounts_;
    std::set<std::string, std::less<>> resident_accounts_;
    std::map<std::string, uint64_t, std::less<>> saved_progress_revisions_;
    uint64_t autosave_interval_ticks_ = 0;
    uint64_t last_autosave_tick_ = 0;
    bool has_last_autosave_tick_ = false;
    bool stopped_ = false;
};

}  // namespace snt::game::replication
