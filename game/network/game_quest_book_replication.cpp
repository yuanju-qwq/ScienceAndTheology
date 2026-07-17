// Task-book replication codec and client-side value cache implementation.

#include "game/network/game_quest_book_replication.h"

#include "core/error.h"

#include <bit>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace snt::game::replication {
namespace {

constexpr size_t kMaxQuestBookProgressRecords = 4096;
constexpr size_t kMaxQuestBookObjectivesPerRecord = 1024;
constexpr size_t kMaxQuestBookObjectiveIdBytes = 512;

[[nodiscard]] snt::core::Error protocol_error(std::string message) {
    return {snt::core::ErrorCode::kProtocolError, std::move(message)};
}

void append_u16(std::vector<std::byte>& bytes, uint16_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::byte>(value & 0xffu));
}

void append_u32(std::vector<std::byte>& bytes, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::byte>& bytes, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void append_i32(std::vector<std::byte>& bytes, int32_t value) {
    append_u32(bytes, std::bit_cast<uint32_t>(value));
}

[[nodiscard]] uint16_t read_u16(std::span<const std::byte> bytes, size_t offset) {
    return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset])) << 8u |
           static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[offset + 1]));
}

[[nodiscard]] uint32_t read_u32(std::span<const std::byte> bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(uint32_t); ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] uint64_t read_u64(std::span<const std::byte> bytes, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(uint64_t); ++index) {
        value = (value << 8u) | std::to_integer<uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] int32_t read_i32(std::span<const std::byte> bytes, size_t offset) {
    return std::bit_cast<int32_t>(read_u32(bytes, offset));
}

[[nodiscard]] bool has_embedded_nul(std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] bool is_valid_quest_state(QuestState state) noexcept {
    switch (state) {
        case QuestState::kLocked:
        case QuestState::kInProgress:
        case QuestState::kCompleted:
            return true;
    }
    return false;
}

[[nodiscard]] snt::core::Expected<void> append_short_string(
    std::vector<std::byte>& bytes, std::string_view value, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if ((require_non_empty && value.empty()) || has_embedded_nul(value) ||
        value.size() > maximum || value.size() > std::numeric_limits<uint16_t>::max()) {
        return protocol_error(std::string("Task-book ") + field_name + " is invalid");
    }
    append_u16(bytes, static_cast<uint16_t>(value.size()));
    for (const char byte : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }
    return {};
}

[[nodiscard]] snt::core::Expected<std::string> read_short_string(
    std::span<const std::byte> bytes, size_t& offset, size_t maximum,
    const char* field_name, bool require_non_empty) {
    if (bytes.size() - offset < sizeof(uint16_t)) {
        return protocol_error(std::string("Task-book ") + field_name + " is truncated");
    }
    const size_t size = read_u16(bytes, offset);
    offset += sizeof(uint16_t);
    if ((require_non_empty && size == 0) || size > maximum || bytes.size() - offset < size) {
        return protocol_error(std::string("Task-book ") + field_name + " is invalid");
    }
    std::string value;
    value.reserve(size);
    for (size_t index = 0; index < size; ++index) {
        value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes[offset + index])));
    }
    offset += size;
    if (has_embedded_nul(value)) {
        return protocol_error(std::string("Task-book ") + field_name + " contains a NUL byte");
    }
    return value;
}

[[nodiscard]] snt::core::Expected<void> validate_quest_book_snapshot(
    const QuestBookSnapshot& snapshot) {
    if (snapshot.account_id.empty() || snapshot.content_fingerprint == 0 ||
        snapshot.account_id.size() > kMaxGamePlayerIdBytes ||
        has_embedded_nul(snapshot.account_id) ||
        snapshot.progress.size() > kMaxQuestBookProgressRecords) {
        return protocol_error("Task-book snapshot header is invalid");
    }

    std::unordered_set<std::string> quest_ids;
    quest_ids.reserve(snapshot.progress.size());
    for (const QuestProgressRecord& record : snapshot.progress) {
        if (record.quest_id.empty() || record.quest_id.size() > kMaxGameQuestIdBytes ||
            has_embedded_nul(record.quest_id) || !is_valid_quest_state(record.state) ||
            record.objective_counts.size() > kMaxQuestBookObjectivesPerRecord ||
            !quest_ids.insert(record.quest_id).second) {
            return protocol_error("Task-book snapshot has an invalid or duplicate quest record");
        }
        for (const auto& [objective_id, count] : record.objective_counts) {
            if (objective_id.empty() || objective_id.size() > kMaxQuestBookObjectiveIdBytes ||
                has_embedded_nul(objective_id) || count < 0) {
                return protocol_error("Task-book snapshot has an invalid objective record");
            }
        }
    }
    return {};
}

}  // namespace

snt::core::Expected<std::vector<std::byte>> encode_game_quest_book_snapshot(
    const QuestBookSnapshot& snapshot) {
    if (auto result = validate_quest_book_snapshot(snapshot); !result) return result.error();

    std::vector<std::byte> payload;
    payload.push_back(static_cast<std::byte>(kGameQuestBookReplicationVersion));
    if (auto result = append_short_string(payload, snapshot.account_id,
                                          kMaxGamePlayerIdBytes, "account id", true);
        !result) {
        return result.error();
    }
    append_u64(payload, snapshot.content_fingerprint);
    append_u64(payload, snapshot.progress_revision);
    append_u16(payload, static_cast<uint16_t>(snapshot.progress.size()));
    for (const QuestProgressRecord& record : snapshot.progress) {
        if (auto result = append_short_string(payload, record.quest_id,
                                              kMaxGameQuestIdBytes, "quest id", true);
            !result) {
            return result.error();
        }
        payload.push_back(static_cast<std::byte>(record.state));
        append_u64(payload, record.completed_tick);
        append_u32(payload, record.completion_count);
        payload.push_back(record.reward_claimed ? std::byte{1} : std::byte{0});
        append_u16(payload, static_cast<uint16_t>(record.objective_counts.size()));
        for (const auto& [objective_id, count] : record.objective_counts) {
            if (auto result = append_short_string(payload, objective_id,
                                                  kMaxQuestBookObjectiveIdBytes,
                                                  "objective id", true);
                !result) {
                return result.error();
            }
            append_i32(payload, count);
        }
    }
    return payload;
}

snt::core::Expected<QuestBookSnapshot> decode_game_quest_book_snapshot(
    std::span<const std::byte> payload) {
    if (payload.empty() || std::to_integer<uint8_t>(payload.front()) !=
                               kGameQuestBookReplicationVersion) {
        return protocol_error("Task-book snapshot version is invalid");
    }

    size_t offset = sizeof(uint8_t);
    auto account_id = read_short_string(payload, offset, kMaxGamePlayerIdBytes,
                                        "account id", true);
    if (!account_id) return account_id.error();
    if (payload.size() - offset < sizeof(uint64_t) * 2 + sizeof(uint16_t)) {
        return protocol_error("Task-book snapshot header is truncated");
    }

    QuestBookSnapshot snapshot;
    snapshot.account_id = std::move(*account_id);
    snapshot.content_fingerprint = read_u64(payload, offset);
    offset += sizeof(uint64_t);
    snapshot.progress_revision = read_u64(payload, offset);
    offset += sizeof(uint64_t);
    const size_t record_count = read_u16(payload, offset);
    offset += sizeof(uint16_t);
    if (record_count > kMaxQuestBookProgressRecords) {
        return protocol_error("Task-book snapshot has too many quest records");
    }
    snapshot.progress.reserve(record_count);
    for (size_t record_index = 0; record_index < record_count; ++record_index) {
        QuestProgressRecord record;
        auto quest_id = read_short_string(payload, offset, kMaxGameQuestIdBytes,
                                          "quest id", true);
        if (!quest_id) return quest_id.error();
        if (payload.size() - offset < sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t) +
                                           sizeof(uint8_t) + sizeof(uint16_t)) {
            return protocol_error("Task-book quest record is truncated");
        }
        record.quest_id = std::move(*quest_id);
        record.state = static_cast<QuestState>(std::to_integer<uint8_t>(payload[offset++]));
        record.completed_tick = read_u64(payload, offset);
        offset += sizeof(uint64_t);
        record.completion_count = read_u32(payload, offset);
        offset += sizeof(uint32_t);
        const uint8_t reward_claimed = std::to_integer<uint8_t>(payload[offset++]);
        const size_t objective_count = read_u16(payload, offset);
        offset += sizeof(uint16_t);
        if (!is_valid_quest_state(record.state) || reward_claimed > 1 ||
            objective_count > kMaxQuestBookObjectivesPerRecord) {
            return protocol_error("Task-book quest record is invalid");
        }
        record.reward_claimed = reward_claimed == 1;
        for (size_t objective_index = 0; objective_index < objective_count; ++objective_index) {
            auto objective_id = read_short_string(payload, offset,
                                                  kMaxQuestBookObjectiveIdBytes,
                                                  "objective id", true);
            if (!objective_id) return objective_id.error();
            if (payload.size() - offset < sizeof(uint32_t)) {
                return protocol_error("Task-book objective count is truncated");
            }
            const int32_t count = read_i32(payload, offset);
            offset += sizeof(uint32_t);
            if (count < 0 || !record.objective_counts.emplace(std::move(*objective_id), count).second) {
                return protocol_error("Task-book objective record is invalid or duplicated");
            }
        }
        snapshot.progress.push_back(std::move(record));
    }

    if (offset != payload.size()) return protocol_error("Task-book snapshot has trailing bytes");
    if (auto result = validate_quest_book_snapshot(snapshot); !result) return result.error();
    return snapshot;
}

GameClientQuestBookState::GameClientQuestBookState(std::string local_account_id)
    : local_account_id_(std::move(local_account_id)) {}

snt::core::Expected<void> GameClientQuestBookState::apply(const GameSnapshot& snapshot) {
    if (snapshot.snapshot_id == 0) {
        return protocol_error("Client task-book state received an invalid snapshot id");
    }

    const GameReplicationValue* quest_book = nullptr;
    for (const GameReplicationValue& value : snapshot.values) {
        if (value.kind != GameReplicationValueKind::kQuestBook) continue;
        if (quest_book != nullptr || value.operation != GameReplicationValueOperation::kUpsert) {
            return protocol_error("Client task-book snapshot has an invalid value record");
        }
        quest_book = &value;
    }

    snapshot_.reset();
    if (quest_book != nullptr) {
        if (auto result = apply_upsert(quest_book->payload, false); !result) return result.error();
    }
    active_snapshot_id_ = snapshot.snapshot_id;
    last_delta_sequence_ = 0;
    return {};
}

snt::core::Expected<void> GameClientQuestBookState::apply(const GameDelta& delta) {
    if (active_snapshot_id_ == 0 || delta.base_snapshot_id != active_snapshot_id_) {
        return protocol_error("Client task-book delta does not match the active snapshot");
    }
    if (delta.sequence == 0 ||
        (last_delta_sequence_ != 0 && delta.sequence != last_delta_sequence_ + 1)) {
        return protocol_error("Client task-book delta sequence is invalid");
    }

    const GameReplicationValue* quest_book = nullptr;
    for (const GameReplicationValue& value : delta.values) {
        if (value.kind != GameReplicationValueKind::kQuestBook) continue;
        if (quest_book != nullptr) {
            return protocol_error("Client task-book delta has duplicate value records");
        }
        quest_book = &value;
    }
    if (quest_book != nullptr) {
        switch (quest_book->operation) {
            case GameReplicationValueOperation::kUpsert:
                if (auto result = apply_upsert(quest_book->payload, true); !result) {
                    return result.error();
                }
                break;
            case GameReplicationValueOperation::kRemove:
                if (!quest_book->payload.empty()) {
                    return protocol_error("Client task-book remove carries a payload");
                }
                snapshot_.reset();
                break;
        }
    }
    last_delta_sequence_ = delta.sequence;
    return {};
}

void GameClientQuestBookState::clear() noexcept {
    snapshot_.reset();
    active_snapshot_id_ = 0;
    last_delta_sequence_ = 0;
}

snt::core::Expected<void> GameClientQuestBookState::apply_upsert(
    std::span<const std::byte> payload, bool require_newer_revision) {
    auto decoded = decode_game_quest_book_snapshot(payload);
    if (!decoded) return decoded.error();
    if (decoded->account_id != local_account_id_) {
        return protocol_error("Client task-book snapshot belongs to a different account");
    }
    if (require_newer_revision && snapshot_ &&
        (decoded->progress_revision < snapshot_->progress_revision ||
         (decoded->progress_revision == snapshot_->progress_revision &&
          decoded->content_fingerprint == snapshot_->content_fingerprint))) {
        return protocol_error("Client task-book delta did not advance the progress revision");
    }
    snapshot_ = std::move(*decoded);
    return {};
}

}  // namespace snt::game::replication
