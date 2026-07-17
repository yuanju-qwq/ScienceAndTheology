// Task-book replication codec and client-side value cache.
//
// QuestRegistry remains authoritative on the server. This module only moves
// validated QuestBookSnapshot values through the SNTG presentation boundary.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"
#include "game/quest/quest_book.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace snt::game::replication {

inline constexpr uint8_t kGameQuestBookReplicationVersion = 2;

[[nodiscard]] snt::core::Expected<std::vector<std::byte>> encode_game_quest_book_snapshot(
    const QuestBookSnapshot& snapshot);
[[nodiscard]] snt::core::Expected<QuestBookSnapshot> decode_game_quest_book_snapshot(
    std::span<const std::byte> payload);

// This value cache is the only task-book state a graphical client receives.
// A future presenter reads copies from it and sends reward claims through the
// dedicated command path; it never writes quest progress locally.
class GameClientQuestBookState final {
public:
    explicit GameClientQuestBookState(std::string local_account_id);

    [[nodiscard]] snt::core::Expected<void> apply(const GameSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> apply(const GameDelta& delta);

    [[nodiscard]] const QuestBookSnapshot* snapshot() const noexcept {
        return snapshot_ ? &*snapshot_ : nullptr;
    }
    [[nodiscard]] uint64_t active_snapshot_id() const noexcept { return active_snapshot_id_; }
    void clear() noexcept;

private:
    [[nodiscard]] snt::core::Expected<void> apply_upsert(
        std::span<const std::byte> payload, bool require_newer_revision);

    std::string local_account_id_;
    std::optional<QuestBookSnapshot> snapshot_;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
};

}  // namespace snt::game::replication
