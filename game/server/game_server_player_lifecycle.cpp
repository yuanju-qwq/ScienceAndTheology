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
                                                     GameServerPlayerState& player_state,
                                                     std::string universe_save_dir,
                                                     uint64_t autosave_interval_ticks)
    : quests_(&quests), player_state_(&player_state), persistence_(universe_save_dir),
      player_state_persistence_(std::move(universe_save_dir)),
      autosave_interval_ticks_(autosave_interval_ticks) {}

snt::core::Expected<void> GameServerPlayerLifecycle::on_peer_authenticated(
    const GameAuthenticatedPeer& peer,
    const snt::network::ReplicationTickContext& context) {
    if (stopped_) return invalid_state("Game server player lifecycle is stopped");
    if (quests_ == nullptr) return invalid_state("Game server player lifecycle has no QuestRegistry");
    if (player_state_ == nullptr) {
        return invalid_state("Game server player lifecycle has no authoritative player state");
    }
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
    auto player_state = load_player_state(peer.identity.account_id);
    if (!player_state) {
        auto error = player_state.error();
        error.with_context("GameServerPlayerLifecycle::on_peer_authenticated(load player state)");
        return error;
    }
    if (auto result = player_state_->on_peer_authenticated(peer, *player_state); !result) {
        auto error = result.error();
        error.with_context("GameServerPlayerLifecycle::on_peer_authenticated(player state)");
        return error;
    }

    active_peers_.emplace(peer.peer, peer.identity.account_id);
    active_sessions_.emplace(peer.peer, peer);
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
    if (player_state_ == nullptr) {
        return invalid_state("Game server player lifecycle has no authoritative player state");
    }
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

    if (auto result = player_state_->on_peer_replaced(previous_peer, replacement_peer); !result) {
        auto error = result.error();
        error.with_context("GameServerPlayerLifecycle::on_peer_replaced(player state)");
        return error;
    }

    active_peers_.erase(previous);
    active_peers_.emplace(replacement_peer.peer, replacement_peer.identity.account_id);
    active_sessions_.erase(previous_peer.peer);
    active_sessions_.emplace(replacement_peer.peer, replacement_peer);
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

    if (auto result = save_player_state(peer); !result) {
        SNT_LOG_ERROR("Unable to save player state for disconnected account '%s': %s",
                      active->second.c_str(), result.error().format().c_str());
    } else {
        SNT_LOG_INFO("Saved player state for disconnected account '%s' (%.*s)",
                     active->second.c_str(), static_cast<int>(reason.size()), reason.data());
    }
    if (auto result = save_account(active->second); !result) {
        SNT_LOG_ERROR("Unable to save quest progress for disconnected player '%s': %s",
                      active->second.c_str(), result.error().format().c_str());
    } else {
        SNT_LOG_INFO("Saved quest progress for disconnected player '%s' (%.*s)",
                     active->second.c_str(), static_cast<int>(reason.size()), reason.data());
    }
    if (player_state_ != nullptr) player_state_->on_peer_disconnected(peer, reason);
    dirty_player_accounts_.erase(active->second);
    active_accounts_.erase(active->second);
    active_peers_.erase(active);
    active_sessions_.erase(peer.peer);
}

snt::core::Expected<void> GameServerPlayerLifecycle::mark_player_state_dirty(
    const GameAuthenticatedPeer& peer) {
    if (stopped_) return invalid_state("Game server player lifecycle is stopped");
    if (auto result = validate_peer(peer); !result) return result.error();
    const auto active = active_peers_.find(peer.peer);
    if (active == active_peers_.end() || active->second != peer.identity.account_id) {
        return invalid_state("Player state checkpoint requires an active authenticated peer");
    }
    dirty_player_accounts_.insert(active->second);
    return {};
}

snt::core::Expected<void> GameServerPlayerLifecycle::flush_all() {
    if (stopped_) return invalid_state("Game server player lifecycle is stopped");
    if (quests_ == nullptr) return invalid_state("Game server player lifecycle has no QuestRegistry");

    std::optional<snt::core::Error> first_error;
    size_t saved_quests = 0;
    size_t saved_players = 0;
    std::vector<GameAuthenticatedPeer> sessions;
    sessions.reserve(active_sessions_.size());
    for (const auto& [peer, session] : active_sessions_) {
        static_cast<void>(peer);
        sessions.push_back(session);
    }
    for (const GameAuthenticatedPeer& session : sessions) {
        if (auto result = save_player_state(session); !result) {
            auto error = result.error();
            error.with_context("GameServerPlayerLifecycle::flush_all(player state)");
            SNT_LOG_ERROR("Unable to flush player state for account '%s': %s",
                          session.identity.account_id.c_str(), error.format().c_str());
            if (!first_error.has_value()) first_error = std::move(error);
            continue;
        }
        dirty_player_accounts_.erase(session.identity.account_id);
        ++saved_players;
    }
    if (auto result = flush_pending_player_states(); !result) {
        auto error = result.error();
        error.with_context("GameServerPlayerLifecycle::flush_all(pending player state)");
        SNT_LOG_ERROR("Unable to flush cached disconnected player state: %s", error.format().c_str());
        if (!first_error.has_value()) first_error = std::move(error);
    }
    for (const std::string& account_id : resident_accounts_) {
        if (auto result = save_account(account_id); !result) {
            auto error = result.error();
            error.with_context("GameServerPlayerLifecycle::flush_all");
            SNT_LOG_ERROR("Unable to flush quest progress for player '%s': %s",
                          account_id.c_str(), error.format().c_str());
            if (!first_error.has_value()) first_error = std::move(error);
            continue;
        }
        ++saved_quests;
    }
    if (saved_players != 0 || saved_quests != 0) {
        SNT_LOG_INFO("Flushed player state for %zu active account(s) and quest progress for %zu resident account(s)",
                     saved_players, saved_quests);
    }
    if (first_error.has_value()) return std::move(*first_error);
    return {};
}

snt::core::Expected<void> GameServerPlayerLifecycle::flush_due(uint64_t tick_index) {
    if (stopped_) return invalid_state("Game server player lifecycle is stopped");
    if (quests_ == nullptr) return invalid_state("Game server player lifecycle has no QuestRegistry");
    if (player_state_ == nullptr) {
        return invalid_state("Game server player lifecycle has no authoritative player state");
    }
    if (autosave_interval_ticks_ == 0) return {};
    if (has_last_autosave_tick_ && tick_index >= last_autosave_tick_ &&
        tick_index - last_autosave_tick_ < autosave_interval_ticks_) {
        return {};
    }

    last_autosave_tick_ = tick_index;
    has_last_autosave_tick_ = true;

    std::optional<snt::core::Error> first_error;
    size_t saved_quests = 0;
    size_t saved_players = 0;
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
        ++saved_quests;
    }
    for (const auto& [account_id, peer_id] : active_accounts_) {
        if (!dirty_player_accounts_.contains(account_id)) continue;
        const auto session = active_sessions_.find(peer_id);
        if (session == active_sessions_.end()) {
            auto error = invalid_state("Player state checkpoint has no active peer session");
            error.with_context("GameServerPlayerLifecycle::flush_due");
            SNT_LOG_ERROR("Unable to autosave player state for account '%s': %s",
                          account_id.c_str(), error.format().c_str());
            if (!first_error.has_value()) first_error = std::move(error);
            continue;
        }
        if (auto result = save_player_state(session->second); !result) {
            auto error = result.error();
            error.with_context("GameServerPlayerLifecycle::flush_due");
            SNT_LOG_ERROR("Unable to autosave player state for account '%s': %s",
                          account_id.c_str(), error.format().c_str());
            if (!first_error.has_value()) first_error = std::move(error);
            continue;
        }
        dirty_player_accounts_.erase(account_id);
        ++saved_players;
    }
    if (saved_quests != 0) {
        SNT_LOG_INFO("Autosaved quest progress for %zu changed resident player account(s) at tick %llu",
                     saved_quests, static_cast<unsigned long long>(tick_index));
    }
    if (saved_players != 0) {
        SNT_LOG_INFO("Autosaved authoritative state for %zu changed player account(s) at tick %llu",
                     saved_players, static_cast<unsigned long long>(tick_index));
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
    active_sessions_.clear();
    active_accounts_.clear();
    resident_accounts_.clear();
    saved_progress_revisions_.clear();
    dirty_player_accounts_.clear();
    pending_player_states_.clear();
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

snt::core::Expected<GamePlayerPersistentState> GameServerPlayerLifecycle::load_player_state(
    std::string_view account_id) {
    if (player_state_ == nullptr) {
        return invalid_state("Game server player lifecycle has no authoritative player state");
    }
    const auto pending = pending_player_states_.find(account_id);
    if (pending != pending_player_states_.end()) return pending->second;

    auto loaded = player_state_persistence_.load_player_state(account_id);
    if (!loaded) {
        auto error = loaded.error();
        error.with_context("GameServerPlayerLifecycle::load_player_state");
        return error;
    }
    if (loaded->has_value()) return std::move(**loaded);
    return player_state_->default_persistent_state();
}

snt::core::Expected<void> GameServerPlayerLifecycle::save_player_state(
    const GameAuthenticatedPeer& peer) {
    if (player_state_ == nullptr) {
        return invalid_state("Game server player lifecycle has no authoritative player state");
    }
    auto captured = player_state_->capture_persistent_state(peer);
    if (!captured) {
        auto error = captured.error();
        error.with_context("GameServerPlayerLifecycle::save_player_state(capture)");
        return error;
    }
    if (auto result = player_state_persistence_.save_player_state(
            peer.identity.account_id, *captured);
        !result) {
        pending_player_states_.insert_or_assign(peer.identity.account_id, std::move(*captured));
        auto error = result.error();
        error.with_context("GameServerPlayerLifecycle::save_player_state(write)");
        return error;
    }
    pending_player_states_.erase(peer.identity.account_id);
    return {};
}

snt::core::Expected<void> GameServerPlayerLifecycle::flush_pending_player_states() {
    std::optional<snt::core::Error> first_error;
    for (auto pending = pending_player_states_.begin();
         pending != pending_player_states_.end();) {
        if (auto result = player_state_persistence_.save_player_state(
                pending->first, pending->second);
            !result) {
            if (!first_error.has_value()) first_error = result.error();
            ++pending;
            continue;
        }
        pending = pending_player_states_.erase(pending);
    }
    if (first_error.has_value()) return std::move(*first_error);
    return {};
}

}  // namespace snt::game::replication
