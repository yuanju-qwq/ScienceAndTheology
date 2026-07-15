// Dedicated-server authenticated-player persistence lifecycle implementation.

#define SNT_LOG_CHANNEL "game.server_players"
#include "game/server/game_server_player_lifecycle.h"

#include "core/error.h"
#include "core/log.h"

#include <optional>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

}  // namespace

GameServerPlayerLifecycle::GameServerPlayerLifecycle(QuestRegistry& quests,
                                                     std::string universe_save_dir,
                                                     uint64_t autosave_interval_ticks)
    : quests_(&quests), persistence_(std::move(universe_save_dir)),
      autosave_interval_ticks_(autosave_interval_ticks) {}

snt::core::Expected<void> GameServerPlayerLifecycle::on_peer_authenticated(
    const GameAuthenticatedPeer& peer,
    const snt::network::ReplicationTickContext& context) {
    if (stopped_) return invalid_state("Game server player lifecycle is stopped");
    if (quests_ == nullptr) return invalid_state("Game server player lifecycle has no QuestRegistry");
    if (auto result = validate_peer(peer); !result) return result.error();
    if (active_peers_.contains(peer.peer)) {
        return invalid_state("Authenticated player lifecycle already owns this transport peer");
    }
    if (active_accounts_.contains(peer.identity.account_id)) {
        return invalid_state("Authenticated player lifecycle already has this account online");
    }

    const auto [resident, inserted] = resident_accounts_.emplace(peer.identity.account_id);
    if (inserted) {
        if (auto result = quests_->load_player_progress(peer.identity.account_id, persistence_); !result) {
            resident_accounts_.erase(resident);
            auto error = result.error();
            error.with_context("GameServerPlayerLifecycle::on_peer_authenticated(load quest progress)");
            return error;
        }
        saved_progress_revisions_[peer.identity.account_id] =
            quests_->progress_revision(peer.identity.account_id);
    }

    active_peers_.emplace(peer.peer, peer.identity.account_id);
    active_accounts_.emplace(peer.identity.account_id, peer.peer);
    SNT_LOG_INFO("Player account '%s' became active on peer %llu at tick %llu (%s task state)",
                 peer.identity.account_id.c_str(), static_cast<unsigned long long>(peer.peer),
                 static_cast<unsigned long long>(context.tick_index),
                 inserted ? "loaded" : "resident");
    return {};
}

snt::core::Expected<void> GameServerPlayerLifecycle::on_peer_replaced(
    const GameAuthenticatedPeer& previous_peer, const GameAuthenticatedPeer& replacement_peer,
    const snt::network::ReplicationTickContext& context) {
    if (stopped_) return invalid_state("Game server player lifecycle is stopped");
    if (auto result = validate_peer(previous_peer); !result) return result.error();
    if (auto result = validate_peer(replacement_peer); !result) return result.error();
    if (previous_peer.peer == replacement_peer.peer) {
        return invalid_argument("Player account takeover requires distinct transport peers");
    }
    if (previous_peer.identity.account_id != replacement_peer.identity.account_id) {
        return invalid_argument("Player account takeover requires the same stable account id");
    }

    const auto previous = active_peers_.find(previous_peer.peer);
    const auto account = active_accounts_.find(previous_peer.identity.account_id);
    if (previous == active_peers_.end() || account == active_accounts_.end() ||
        previous->second != previous_peer.identity.account_id || account->second != previous_peer.peer) {
        return invalid_state("Player account takeover has no matching active session");
    }
    if (active_peers_.contains(replacement_peer.peer)) {
        return invalid_state("Player account takeover peer is already active");
    }

    active_peers_.erase(previous);
    active_peers_.emplace(replacement_peer.peer, replacement_peer.identity.account_id);
    account->second = replacement_peer.peer;
    SNT_LOG_INFO("Player account '%s' transferred from peer %llu to peer %llu at tick %llu without reload",
                 replacement_peer.identity.account_id.c_str(),
                 static_cast<unsigned long long>(previous_peer.peer),
                 static_cast<unsigned long long>(replacement_peer.peer),
                 static_cast<unsigned long long>(context.tick_index));
    return {};
}

void GameServerPlayerLifecycle::on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                                     std::string_view reason) noexcept {
    if (stopped_ || peer.peer == snt::network::kInvalidPeerId) return;

    const auto active = active_peers_.find(peer.peer);
    if (active == active_peers_.end() || active->second != peer.identity.account_id) return;

    if (auto result = save_account(active->second); !result) {
        SNT_LOG_ERROR("Unable to save quest progress for disconnected player '%s': %s",
                      active->second.c_str(), result.error().format().c_str());
    } else {
        SNT_LOG_INFO("Saved quest progress for disconnected player '%s' (%.*s)",
                     active->second.c_str(), static_cast<int>(reason.size()), reason.data());
    }
    active_accounts_.erase(active->second);
    active_peers_.erase(active);
}

snt::core::Expected<void> GameServerPlayerLifecycle::flush_all() {
    if (stopped_) return invalid_state("Game server player lifecycle is stopped");
    if (quests_ == nullptr) return invalid_state("Game server player lifecycle has no QuestRegistry");

    std::optional<snt::core::Error> first_error;
    size_t saved = 0;
    for (const std::string& account_id : resident_accounts_) {
        if (auto result = save_account(account_id); !result) {
            auto error = result.error();
            error.with_context("GameServerPlayerLifecycle::flush_all");
            SNT_LOG_ERROR("Unable to flush quest progress for player '%s': %s",
                          account_id.c_str(), error.format().c_str());
            if (!first_error.has_value()) first_error = std::move(error);
            continue;
        }
        ++saved;
    }
    if (saved != 0) {
        SNT_LOG_INFO("Flushed quest progress for %zu resident player account(s)", saved);
    }
    if (first_error.has_value()) return std::move(*first_error);
    return {};
}

snt::core::Expected<void> GameServerPlayerLifecycle::flush_due(uint64_t tick_index) {
    if (stopped_) return invalid_state("Game server player lifecycle is stopped");
    if (quests_ == nullptr) return invalid_state("Game server player lifecycle has no QuestRegistry");
    if (autosave_interval_ticks_ == 0) return {};
    if (has_last_autosave_tick_ && tick_index >= last_autosave_tick_ &&
        tick_index - last_autosave_tick_ < autosave_interval_ticks_) {
        return {};
    }

    last_autosave_tick_ = tick_index;
    has_last_autosave_tick_ = true;

    std::optional<snt::core::Error> first_error;
    size_t saved = 0;
    for (const std::string& account_id : resident_accounts_) {
        const uint64_t revision = quests_->progress_revision(account_id);
        const auto saved_revision = saved_progress_revisions_.find(account_id);
        if (saved_revision != saved_progress_revisions_.end() &&
            saved_revision->second == revision) {
            continue;
        }
        if (auto result = save_account(account_id); !result) {
            auto error = result.error();
            error.with_context("GameServerPlayerLifecycle::flush_due");
            SNT_LOG_ERROR("Unable to autosave quest progress for player '%s': %s",
                          account_id.c_str(), error.format().c_str());
            if (!first_error.has_value()) first_error = std::move(error);
            continue;
        }
        ++saved;
    }
    if (saved != 0) {
        SNT_LOG_INFO("Autosaved quest progress for %zu changed resident player account(s) at tick %llu",
                     saved, static_cast<unsigned long long>(tick_index));
    }
    if (first_error.has_value()) return std::move(*first_error);
    return {};
}

void GameServerPlayerLifecycle::shutdown() noexcept {
    if (stopped_) return;
    if (auto result = flush_all(); !result) {
        SNT_LOG_ERROR("Final quest progress flush failed: %s", result.error().format().c_str());
    }
    stopped_ = true;
    active_peers_.clear();
    active_accounts_.clear();
    resident_accounts_.clear();
    saved_progress_revisions_.clear();
}

snt::core::Expected<void> GameServerPlayerLifecycle::validate_peer(
    const GameAuthenticatedPeer& peer) const {
    if (peer.peer == snt::network::kInvalidPeerId) {
        return invalid_argument("Player lifecycle requires an authenticated transport peer");
    }
    if (auto result = validate_player_identity(peer.identity); !result) {
        auto error = result.error();
        error.with_context("GameServerPlayerLifecycle::validate_peer(identity)");
        return error;
    }
    return {};
}

snt::core::Expected<void> GameServerPlayerLifecycle::save_account(
    std::string_view account_id) {
    if (quests_ == nullptr) return invalid_state("Game server player lifecycle has no QuestRegistry");
    if (auto result = quests_->save_player_progress(account_id, persistence_); !result) {
        auto error = result.error();
        error.with_context("GameServerPlayerLifecycle::save_account");
        return error;
    }
    saved_progress_revisions_[std::string(account_id)] = quests_->progress_revision(account_id);
    return {};
}

}  // namespace snt::game::replication
