#define SNT_LOG_CHANNEL "save"
#include "save_manager.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "chunk_serializer.h"
#include "voxel/storage/region_file.h"

#include "core/log.h"

namespace fs = std::filesystem;

namespace snt::game {

using snt::voxel::VoxelRegionEntry;
using snt::voxel::VoxelRegionFile;

namespace {

fs::path utf8_path(const std::string& path) {
    return fs::u8path(path);
}

std::string path_to_utf8(const fs::path& path) {
    const auto encoded = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::string region_file_path(const std::string& planet_dir,
                             const std::string& dimension_id,
                             int region_x, int region_y, int region_z) {
    return planet_dir + "/regions/" + VoxelRegionFile::region_file_name(
        dimension_id, region_x, region_y, region_z);
}

bool ensure_planet_header_if_missing(const std::string& planet_dir,
                                     int64_t seed,
                                     const std::string& dimension_id) {
    const std::string path = planet_dir + "/planet_data.bin";
    if (fs::exists(utf8_path(path))) {
        return true;
    }
    return GameSaveManager::write_planet_data(planet_dir, seed, dimension_id, nullptr);
}

bool read_existing_region(const std::string& file_path,
                          const std::string& expected_dimension,
                          int expected_rx, int expected_ry, int expected_rz,
                          std::vector<VoxelRegionEntry>& entries) {
    entries.clear();
    const fs::path path = utf8_path(file_path);
    if (!fs::exists(path)) {
        return true;
    }

    std::string file_dimension_id;
    int file_rx = 0;
    int file_ry = 0;
    int file_rz = 0;
    if (!VoxelRegionFile::read(file_path, file_dimension_id,
                          file_rx, file_ry, file_rz, entries)) {
        return false;
    }
    return file_dimension_id == expected_dimension
        && file_rx == expected_rx
        && file_ry == expected_ry
        && file_rz == expected_rz;
}

GameChunk assemble_game_chunk(const VoxelChunk& voxel,
                              const GameChunkSidecar* sidecar) {
    GameChunk result;
    result.voxel_chunk() = voxel;
    if (sidecar) result.sidecar() = *sidecar;
    return result;
}

constexpr char kQuestProgressMagic[] = {'S', 'N', 'T', 'Q'};
constexpr char kPlayerStateMagic[] = {'S', 'N', 'T', 'P'};
constexpr size_t kMaxQuestPlayerIdBytes = 256;
constexpr uint32_t kMaxQuestProgressRecords = 4096;
constexpr uint32_t kMaxQuestIdBytes = 512;
constexpr uint32_t kMaxQuestObjectiveIdBytes = 512;
constexpr uint32_t kMaxQuestObjectivesPerRecord = 256;
constexpr size_t kMaxPlayerStateDimensionIdBytes = 128;
constexpr size_t kMaxPlayerStateResourceTypeBytes = 64;
constexpr size_t kMaxPlayerStateResourceIdBytes = 256;
constexpr size_t kMaxPlayerStateResourceVariantBytes = 16u * 1024u;
constexpr size_t kMaxPlayerStateItemInstanceBytes = 16u * 1024u;
constexpr uint32_t kMaxPlayerStateInventorySlots = 256;
constexpr int32_t kMaxPlayerStateStackSize = 65536;
constexpr size_t kMaxPlayerStateOrganSchemaIdBytes = 128;
constexpr size_t kMaxPlayerStateOrganPayloadBytes = 64u * 1024u;

snt::core::Error persistence_error(snt::core::ErrorCode code, std::string message) {
    return {code, std::move(message)};
}

bool is_valid_quest_state(QuestState state) noexcept {
    switch (state) {
        case QuestState::kLocked:
        case QuestState::kInProgress:
        case QuestState::kCompleted:
            return true;
    }
    return false;
}

snt::core::Expected<void> validate_quest_progress_request(std::string_view save_dir,
                                                           std::string_view player_id) {
    if (save_dir.empty()) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Quest progress save directory must not be empty");
    }
    if (player_id.empty() || player_id.size() > kMaxQuestPlayerIdBytes) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Quest progress player id must contain 1 to 256 bytes");
    }
    return {};
}

snt::core::Expected<void> validate_quest_progress_records(
    std::span<const QuestProgressRecord> progress) {
    if (progress.size() > kMaxQuestProgressRecords) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Quest progress contains too many quest records");
    }

    std::set<std::string, std::less<>> quest_ids;
    for (const QuestProgressRecord& record : progress) {
        if (record.quest_id.empty() || record.quest_id.size() > kMaxQuestIdBytes ||
            !is_valid_quest_state(record.state) ||
            record.objective_counts.size() > kMaxQuestObjectivesPerRecord ||
            !quest_ids.emplace(record.quest_id).second) {
            return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                     "Quest progress contains an invalid or duplicate quest record");
        }
        for (const auto& [objective_id, count] : record.objective_counts) {
            if (objective_id.empty() || objective_id.size() > kMaxQuestObjectiveIdBytes || count < 0) {
                return persistence_error(
                    snt::core::ErrorCode::kInvalidArgument,
                    "Quest progress contains an invalid objective record");
            }
        }
    }
    return {};
}

std::string hex_player_id(std::string_view player_id) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(player_id.size() * 2);
    for (const unsigned char byte : player_id) {
        encoded.push_back(kHex[(byte >> 4) & 0x0F]);
        encoded.push_back(kHex[byte & 0x0F]);
    }
    return encoded;
}

fs::path quest_progress_directory(std::string_view save_dir) {
    return utf8_path(std::string(save_dir)) / "players";
}

fs::path quest_progress_path(std::string_view save_dir, std::string_view player_id) {
    return quest_progress_directory(save_dir) / ("player_" + hex_player_id(player_id) + ".quest");
}

fs::path player_state_path(std::string_view save_dir, std::string_view player_id) {
    return quest_progress_directory(save_dir) / ("player_" + hex_player_id(player_id) + ".player");
}

template <typename T>
bool write_value(std::ofstream& file, const T& value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return file.good();
}

template <typename T>
bool read_value(std::ifstream& file, T& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return file.good();
}

bool write_quest_string(std::ofstream& file, std::string_view value, uint32_t max_length) {
    if (value.size() > max_length || value.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    const uint32_t length = static_cast<uint32_t>(value.size());
    if (!write_value(file, length)) return false;
    if (length == 0) return true;
    file.write(value.data(), static_cast<std::streamsize>(length));
    return file.good();
}

bool read_quest_string(std::ifstream& file, std::string& value, uint32_t max_length) {
    uint32_t length = 0;
    if (!read_value(file, length) || length > max_length) return false;
    value.resize(length);
    if (length == 0) return true;
    file.read(value.data(), static_cast<std::streamsize>(length));
    return file.good();
}

bool write_quest_progress_record(std::ofstream& file, const QuestProgressRecord& record) {
    const uint8_t state = static_cast<uint8_t>(record.state);
    const uint8_t reward_claimed = record.reward_claimed ? 1 : 0;
    const uint32_t objective_count = static_cast<uint32_t>(record.objective_counts.size());
    if (!write_quest_string(file, record.quest_id, kMaxQuestIdBytes) ||
        !write_value(file, state) ||
        !write_value(file, record.completed_tick) ||
        !write_value(file, record.completion_count) ||
        !write_value(file, reward_claimed) ||
        !write_value(file, objective_count)) {
        return false;
    }
    for (const auto& [objective_id, count] : record.objective_counts) {
        if (!write_quest_string(file, objective_id, kMaxQuestObjectiveIdBytes) ||
            !write_value(file, count)) {
            return false;
        }
    }
    return true;
}

bool read_quest_progress_record(std::ifstream& file, QuestProgressRecord& record) {
    QuestProgressRecord restored;
    uint8_t state = 0;
    uint8_t reward_claimed = 0;
    uint32_t objective_count = 0;
    if (!read_quest_string(file, restored.quest_id, kMaxQuestIdBytes) ||
        restored.quest_id.empty() ||
        !read_value(file, state) ||
        !read_value(file, restored.completed_tick) ||
        !read_value(file, restored.completion_count) ||
        !read_value(file, reward_claimed) || reward_claimed > 1 ||
        !read_value(file, objective_count) || objective_count > kMaxQuestObjectivesPerRecord) {
        return false;
    }
    restored.state = static_cast<QuestState>(state);
    if (!is_valid_quest_state(restored.state)) return false;

    for (uint32_t index = 0; index < objective_count; ++index) {
        std::string objective_id;
        int32_t count = 0;
        if (!read_quest_string(file, objective_id, kMaxQuestObjectiveIdBytes) ||
            objective_id.empty() || !read_value(file, count) || count < 0 ||
            !restored.objective_counts.emplace(std::move(objective_id), count).second) {
            return false;
        }
    }
    restored.reward_claimed = reward_claimed == 1;
    record = std::move(restored);
    return true;
}

snt::core::Expected<void> replace_recoverable_player_file(const fs::path& final_path,
                                                           const fs::path& temporary_path,
                                                           const fs::path& backup_path,
                                                           std::string_view label) {
    std::error_code ec;
    const bool has_final = fs::exists(final_path, ec);
    if (ec) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to inspect " + std::string(label) + " destination: " + ec.message());
    }
    const bool has_backup = fs::exists(backup_path, ec);
    if (ec) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to inspect " + std::string(label) + " backup: " + ec.message());
    }

    if (!has_final && has_backup) {
        fs::rename(backup_path, final_path, ec);
        if (ec) {
            return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                     "Unable to recover previous " + std::string(label) + ": " + ec.message());
        }
    } else if (has_final && has_backup) {
        fs::remove(backup_path, ec);
        if (ec) {
            return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                     "Unable to remove stale " + std::string(label) + " backup: " + ec.message());
        }
    }

    const bool primary_exists = fs::exists(final_path, ec);
    if (ec) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to inspect " + std::string(label) + " primary file: " + ec.message());
    }
    if (primary_exists) {
        fs::rename(final_path, backup_path, ec);
        if (ec) {
            return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                     "Unable to stage previous " + std::string(label) + ": " + ec.message());
        }
    }

    fs::rename(temporary_path, final_path, ec);
    if (!ec) {
        fs::remove(backup_path, ec);
        return {};
    }

    const std::string rename_error = ec.message();
    std::error_code restore_error;
    const bool final_exists_after_failure = fs::exists(final_path, restore_error);
    if (!restore_error && !final_exists_after_failure && fs::exists(backup_path, restore_error) &&
        !restore_error) {
        fs::rename(backup_path, final_path, restore_error);
    }
    return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                             "Unable to promote " + std::string(label) + " file: " + rename_error);
}

snt::core::Expected<void> validate_player_state_request(std::string_view save_dir,
                                                         std::string_view player_id) {
    if (save_dir.empty()) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Player state save directory must not be empty");
    }
    if (player_id.empty() || player_id.size() > kMaxQuestPlayerIdBytes) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Player state player id must contain 1 to 256 bytes");
    }
    return {};
}

bool is_empty_player_state_stack(const GamePlayerItemStack& stack) noexcept {
    return stack.is_empty();
}

bool is_valid_player_state_stack(const GamePlayerItemStack& stack,
                                 int32_t max_stack_size,
                                 bool equipment_slot) noexcept {
    if (is_empty_player_state_stack(stack)) return true;
    if (!stack.is_valid_item() ||
        stack.resource.key.type.size() > kMaxPlayerStateResourceTypeBytes ||
        stack.resource.key.id.size() > kMaxPlayerStateResourceIdBytes ||
        stack.resource.key.variant.size() > kMaxPlayerStateResourceVariantBytes ||
        stack.instance_data.size() > kMaxPlayerStateItemInstanceBytes) {
        return false;
    }
    if (equipment_slot || !stack.instance_data.empty()) return stack.resource.amount == 1;
    return stack.resource.amount <= max_stack_size;
}

snt::core::Expected<void> validate_player_state_position(const GamePlayerWorldPosition& position) {
    if (position.dimension_id.empty() ||
        position.dimension_id.size() > kMaxPlayerStateDimensionIdBytes) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Player state contains an invalid dimension id");
    }
    return {};
}

snt::core::Expected<void> validate_player_state_value(const GamePlayerPersistentState& state) {
    if (auto result = validate_player_state_position(state.position); !result) return result.error();
    if (state.respawn_point.has_value()) {
        if (auto result = validate_player_state_position(*state.respawn_point); !result) {
            return result.error();
        }
    }

    const GamePlayerInventory& inventory = state.inventory;
    if (inventory.max_slots == 0 || inventory.max_slots > kMaxPlayerStateInventorySlots ||
        inventory.max_stack_size <= 0 || inventory.max_stack_size > kMaxPlayerStateStackSize ||
        inventory.slots.size() != inventory.max_slots) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Player state contains invalid fixed inventory capacity");
    }
    for (const GamePlayerItemStack& stack : inventory.slots) {
        if (!is_valid_player_state_stack(stack, inventory.max_stack_size, false)) {
            return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                     "Player state contains an invalid inventory stack");
        }
    }
    for (const GamePlayerItemStack& stack : state.equipment.slots) {
        if (!is_valid_player_state_stack(stack, inventory.max_stack_size, true)) {
            return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                     "Player state contains an invalid equipment stack");
        }
    }

    const GamePlayerOrganState& organs = state.organs;
    if (organs.schema_id.empty()) {
        if (organs.schema_version != 0 || !organs.payload.empty()) {
            return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                     "Player state has an incomplete organ schema declaration");
        }
    } else if (organs.schema_id.size() > kMaxPlayerStateOrganSchemaIdBytes ||
               organs.schema_version == 0 || organs.payload.size() > kMaxPlayerStateOrganPayloadBytes) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Player state organ payload exceeds persistence limits");
    }
    return {};
}

bool write_player_state_position(std::ofstream& file, const GamePlayerWorldPosition& position) {
    return write_quest_string(file, position.dimension_id,
                              static_cast<uint32_t>(kMaxPlayerStateDimensionIdBytes)) &&
           write_value(file, position.position.x) &&
           write_value(file, position.position.y) &&
           write_value(file, position.position.z);
}

bool read_player_state_position(std::ifstream& file, GamePlayerWorldPosition& position) {
    GamePlayerWorldPosition restored;
    if (!read_quest_string(file, restored.dimension_id,
                           static_cast<uint32_t>(kMaxPlayerStateDimensionIdBytes)) ||
        !read_value(file, restored.position.x) || !read_value(file, restored.position.y) ||
        !read_value(file, restored.position.z)) {
        return false;
    }
    position = std::move(restored);
    return true;
}

bool write_player_state_stack(std::ofstream& file, const GamePlayerItemStack& stack) {
    const uint8_t present = is_empty_player_state_stack(stack) ? 0 : 1;
    if (!write_value(file, present) || present == 0) return file.good();
    return write_quest_string(file, stack.resource.key.type,
                              static_cast<uint32_t>(kMaxPlayerStateResourceTypeBytes)) &&
           write_quest_string(file, stack.resource.key.id,
                              static_cast<uint32_t>(kMaxPlayerStateResourceIdBytes)) &&
           write_quest_string(file, stack.resource.key.variant,
                              static_cast<uint32_t>(kMaxPlayerStateResourceVariantBytes)) &&
           write_value(file, stack.resource.amount) &&
           write_quest_string(file, stack.instance_data,
                              static_cast<uint32_t>(kMaxPlayerStateItemInstanceBytes));
}

bool read_player_state_stack(std::ifstream& file, GamePlayerItemStack& stack) {
    uint8_t present = 0;
    if (!read_value(file, present) || present > 1) return false;
    if (present == 0) {
        stack = {};
        return true;
    }
    GamePlayerItemStack restored;
    if (!read_quest_string(file, restored.resource.key.type,
                           static_cast<uint32_t>(kMaxPlayerStateResourceTypeBytes)) ||
        !read_quest_string(file, restored.resource.key.id,
                           static_cast<uint32_t>(kMaxPlayerStateResourceIdBytes)) ||
        !read_quest_string(file, restored.resource.key.variant,
                           static_cast<uint32_t>(kMaxPlayerStateResourceVariantBytes)) ||
        !read_value(file, restored.resource.amount) ||
        !read_quest_string(file, restored.instance_data,
                            static_cast<uint32_t>(kMaxPlayerStateItemInstanceBytes))) {
        return false;
    }
    stack = std::move(restored);
    return true;
}

bool write_player_state_value(std::ofstream& file, const GamePlayerPersistentState& state) {
    const uint8_t has_respawn = state.respawn_point.has_value() ? 1 : 0;
    const uint32_t inventory_slot_count = static_cast<uint32_t>(state.inventory.slots.size());
    const uint32_t equipment_slot_count = static_cast<uint32_t>(state.equipment.slots.size());
    const uint32_t organ_payload_size = static_cast<uint32_t>(state.organs.payload.size());
    if (!write_player_state_position(file, state.position) || !write_value(file, has_respawn)) {
        return false;
    }
    if (has_respawn != 0 && !write_player_state_position(file, *state.respawn_point)) return false;
    if (!write_value(file, state.inventory.max_slots) ||
        !write_value(file, state.inventory.max_stack_size) ||
        !write_value(file, inventory_slot_count)) {
        return false;
    }
    for (const GamePlayerItemStack& stack : state.inventory.slots) {
        if (!write_player_state_stack(file, stack)) return false;
    }
    if (!write_value(file, equipment_slot_count)) return false;
    for (const GamePlayerItemStack& stack : state.equipment.slots) {
        if (!write_player_state_stack(file, stack)) return false;
    }
    if (!write_quest_string(file, state.organs.schema_id,
                            static_cast<uint32_t>(kMaxPlayerStateOrganSchemaIdBytes)) ||
        !write_value(file, state.organs.schema_version) ||
        !write_value(file, organ_payload_size)) {
        return false;
    }
    if (organ_payload_size != 0) {
        file.write(reinterpret_cast<const char*>(state.organs.payload.data()),
                   static_cast<std::streamsize>(organ_payload_size));
    }
    return file.good();
}

bool read_player_state_value(std::ifstream& file, GamePlayerPersistentState& state) {
    GamePlayerPersistentState restored;
    uint8_t has_respawn = 0;
    uint32_t inventory_slot_count = 0;
    uint32_t equipment_slot_count = 0;
    uint32_t organ_payload_size = 0;
    if (!read_player_state_position(file, restored.position) || !read_value(file, has_respawn) ||
        has_respawn > 1) {
        return false;
    }
    if (has_respawn != 0) {
        GamePlayerWorldPosition respawn;
        if (!read_player_state_position(file, respawn)) return false;
        restored.respawn_point = std::move(respawn);
    }
    if (!read_value(file, restored.inventory.max_slots) ||
        !read_value(file, restored.inventory.max_stack_size) ||
        !read_value(file, inventory_slot_count) ||
        inventory_slot_count > kMaxPlayerStateInventorySlots ||
        inventory_slot_count != restored.inventory.max_slots) {
        return false;
    }
    restored.inventory.slots.resize(inventory_slot_count);
    for (GamePlayerItemStack& stack : restored.inventory.slots) {
        if (!read_player_state_stack(file, stack)) return false;
    }
    if (!read_value(file, equipment_slot_count) ||
        equipment_slot_count != restored.equipment.slots.size()) {
        return false;
    }
    for (GamePlayerItemStack& stack : restored.equipment.slots) {
        if (!read_player_state_stack(file, stack)) return false;
    }
    if (!read_quest_string(file, restored.organs.schema_id,
                           static_cast<uint32_t>(kMaxPlayerStateOrganSchemaIdBytes)) ||
        !read_value(file, restored.organs.schema_version) ||
        !read_value(file, organ_payload_size) ||
        organ_payload_size > kMaxPlayerStateOrganPayloadBytes) {
        return false;
    }
    restored.organs.payload.resize(organ_payload_size);
    if (organ_payload_size != 0) {
        file.read(reinterpret_cast<char*>(restored.organs.payload.data()),
                  static_cast<std::streamsize>(organ_payload_size));
        if (!file.good()) return false;
    }
    state = std::move(restored);
    return true;
}

} // namespace

std::vector<std::string> GameSaveManager::list_saves(
    const std::string& base_saves_dir) {
    std::vector<std::string> result;

    const fs::path saves_path = utf8_path(base_saves_dir);
    if (!fs::exists(saves_path) || !fs::is_directory(saves_path)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(saves_path)) {
        if (!entry.is_directory()) {
            continue;
        }

        std::string save_path = path_to_utf8(entry.path());
        std::string header_path = save_path + "/universe_header.bin";

        if (fs::exists(utf8_path(header_path)) &&
            fs::is_regular_file(utf8_path(header_path))) {
            result.push_back(path_to_utf8(entry.path().filename()));
        }
    }

    return result;
}

bool GameSaveManager::ensure_directory(const std::string& path) {
    const fs::path directory = utf8_path(path);
    if (fs::exists(directory)) {
        return fs::is_directory(directory);
    }

    std::error_code ec;
    bool created = fs::create_directories(directory, ec);
    return created && !ec;
}

// --- Per-dimension save / load ---

int GameSaveManager::save_dimension(const std::string& planet_dir,
                                    int64_t seed,
                                    const std::string& dimension_id,
                                    const ChunkRegistry& voxel_chunks,
                                    const GameChunkSidecarRegistry& sidecars) {
    if (planet_dir.empty() || dimension_id.empty() ||
        !write_planet_data(planet_dir, seed, dimension_id, nullptr)) {
        return -1;
    }

    std::vector<ChunkKey> loaded_keys;
    for (const ChunkKey& key : voxel_chunks.all_chunk_keys()) {
        if (key.dimension_id == dimension_id) loaded_keys.push_back(key);
    }
    const auto chunk_key_less = [](const ChunkKey& left, const ChunkKey& right) {
        if (left.dimension_id != right.dimension_id) return left.dimension_id < right.dimension_id;
        if (left.chunk_x != right.chunk_x) return left.chunk_x < right.chunk_x;
        if (left.chunk_y != right.chunk_y) return left.chunk_y < right.chunk_y;
        return left.chunk_z < right.chunk_z;
    };
    std::sort(loaded_keys.begin(), loaded_keys.end(), chunk_key_less);

    int saved_count = 0;
    for (const ChunkKey& key : loaded_keys) {
        if (auto saved = save_loaded_chunk(
                planet_dir, seed, dimension_id, voxel_chunks, sidecars,
                key.chunk_x, key.chunk_y, key.chunk_z);
            !saved) {
            return -1;
        }
        ++saved_count;
    }

    std::vector<ChunkKey> sidecar_only_keys;
    sidecars.for_each([&](const ChunkKey& key, const GameChunkSidecar&) {
        if (key.dimension_id != dimension_id ||
            voxel_chunks.has_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z)) {
            return;
        }
        sidecar_only_keys.push_back(key);
    });
    std::sort(sidecar_only_keys.begin(), sidecar_only_keys.end(), chunk_key_less);
    for (const ChunkKey& key : sidecar_only_keys) {
        const GameChunkSidecar* sidecar = sidecars.get(key);
        if (sidecar == nullptr ||
            !save_chunk_sidecar(planet_dir, seed, dimension_id,
                                key.chunk_x, key.chunk_y, key.chunk_z, *sidecar)) {
            return -1;
        }
        ++saved_count;
    }
    return saved_count;
}

snt::core::Expected<size_t> GameSaveManager::load_dimension_sidecar_index(
    const std::string& planet_dir,
    const std::string& dimension_id,
    GameChunkSidecarRegistry& sidecars) {
    if (planet_dir.empty() || dimension_id.empty()) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Sidecar-index load requires a planet directory and dimension id");
    }
    if (sidecars.size() != 0) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Sidecar-index load requires an empty sidecar registry");
    }

    int64_t stored_seed = 0;
    std::string stored_dimension_id;
    GamePlanetSummaryData ignored_summary;
    bool ignored_has_summary = false;
    if (!read_planet_data(planet_dir, stored_seed, stored_dimension_id,
                          ignored_summary, ignored_has_summary) ||
        stored_dimension_id != dimension_id) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Sidecar-index load found an invalid or mismatched dimension header");
    }

    const fs::path regions_path = utf8_path(planet_dir + "/regions");
    std::error_code exists_error;
    const bool regions_exist = fs::exists(regions_path, exists_error);
    if (exists_error) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to inspect sidecar-index region directory: " +
                                     exists_error.message());
    }
    if (!regions_exist) return size_t{0};
    std::error_code directory_error;
    if (!fs::is_directory(regions_path, directory_error) || directory_error) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Sidecar-index region path is not a readable directory");
    }

    std::vector<std::pair<ChunkKey, GameChunkSidecar>> staged_sidecars;
    std::set<std::tuple<int, int, int>> seen_chunk_coordinates;
    const GameChunkSerializer serializer;
    try {
        for (const fs::directory_entry& entry : fs::directory_iterator(regions_path)) {
            if (!entry.is_regular_file()) continue;

            std::string file_dimension_id;
            int region_x = 0;
            int region_y = 0;
            int region_z = 0;
            std::vector<VoxelRegionEntry> entries;
            if (!VoxelRegionFile::read(path_to_utf8(entry.path()), file_dimension_id,
                                       region_x, region_y, region_z, entries)) {
                return persistence_error(
                    snt::core::ErrorCode::kInvalidState,
                    "Sidecar-index load rejected a corrupt or non-current region file");
            }
            if (file_dimension_id != dimension_id) continue;

            for (const VoxelRegionEntry& chunk_entry : entries) {
                const int chunk_x = region_x * VoxelRegionFile::kRegionSize +
                                    static_cast<int>(chunk_entry.local_x);
                const int chunk_y = region_y * VoxelRegionFile::kRegionSize +
                                    static_cast<int>(chunk_entry.local_y);
                const int chunk_z = region_z * VoxelRegionFile::kRegionSize +
                                    static_cast<int>(chunk_entry.local_z);
                if (!seen_chunk_coordinates.emplace(chunk_x, chunk_y, chunk_z).second) {
                    return persistence_error(
                        snt::core::ErrorCode::kInvalidState,
                        "Sidecar-index load found a duplicate persisted chunk coordinate");
                }

                std::string payload_dimension_id;
                GameChunk chunk;
                if (!serializer.deserialize(chunk_entry.payload, payload_dimension_id, chunk) ||
                    payload_dimension_id != dimension_id ||
                    chunk.chunk_x != chunk_x || chunk.chunk_y != chunk_y ||
                    chunk.chunk_z != chunk_z) {
                    return persistence_error(
                        snt::core::ErrorCode::kInvalidState,
                        "Sidecar-index load rejected a mismatched or corrupt chunk payload");
                }
                staged_sidecars.emplace_back(
                    ChunkKey{dimension_id, chunk_x, chunk_y, chunk_z},
                    std::move(chunk.sidecar()));
            }
        }
    } catch (const std::exception& error) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Sidecar-index load failed: " + std::string(error.what()));
    } catch (...) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Sidecar-index load raised an unknown exception");
    }

    for (auto& [key, sidecar] : staged_sidecars) {
        sidecars.set(std::move(key), std::move(sidecar));
    }
    return staged_sidecars.size();
}

snt::core::Expected<std::vector<QuestProgressRecord>>
GameSaveManager::load_quest_progress(const std::string& save_dir, std::string_view player_id) {
    if (auto result = validate_quest_progress_request(save_dir, player_id); !result) {
        return result.error();
    }

    const fs::path primary_path = quest_progress_path(save_dir, player_id);
    fs::path backup_path = primary_path;
    backup_path += ".bak";

    std::error_code ec;
    const bool primary_exists = fs::exists(primary_path, ec);
    if (ec) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to inspect quest progress file: " + ec.message());
    }

    fs::path selected_path = primary_path;
    if (!primary_exists) {
        const bool backup_exists = fs::exists(backup_path, ec);
        if (ec) {
            return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                     "Unable to inspect quest progress backup: " + ec.message());
        }
        if (!backup_exists) return std::vector<QuestProgressRecord>{};

        selected_path = backup_path;
        SNT_LOG_WARN("Quest progress primary file was missing for player '%.*s'; using recovery backup",
                     static_cast<int>(player_id.size()), player_id.data());
    }

    std::ifstream file(selected_path, std::ios::binary);
    if (!file.is_open()) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to open quest progress file: " + path_to_utf8(selected_path));
    }

    char magic[sizeof(kQuestProgressMagic)] = {};
    uint8_t version = 0;
    std::string stored_player_id;
    uint32_t record_count = 0;
    file.read(magic, sizeof(magic));
    if (!file.good() || std::memcmp(magic, kQuestProgressMagic, sizeof(magic)) != 0 ||
        !read_value(file, version) || version != kQuestProgressVersion ||
        !read_quest_string(file, stored_player_id, static_cast<uint32_t>(kMaxQuestPlayerIdBytes)) ||
        stored_player_id != player_id || !read_value(file, record_count) ||
        record_count > kMaxQuestProgressRecords) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Quest progress file is corrupt or not the current format");
    }

    std::vector<QuestProgressRecord> progress;
    progress.reserve(record_count);
    std::set<std::string, std::less<>> quest_ids;
    for (uint32_t index = 0; index < record_count; ++index) {
        QuestProgressRecord record;
        if (!read_quest_progress_record(file, record) ||
            !quest_ids.emplace(record.quest_id).second) {
            return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                     "Quest progress file contains an invalid or duplicate record");
        }
        progress.push_back(std::move(record));
    }

    if (file.peek() != std::char_traits<char>::eof() || file.bad()) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Quest progress file has trailing or unreadable data");
    }
    return progress;
}

snt::core::Expected<void> GameSaveManager::save_quest_progress(
    const std::string& save_dir, std::string_view player_id,
    std::span<const QuestProgressRecord> progress) {
    if (auto result = validate_quest_progress_request(save_dir, player_id); !result) {
        return result.error();
    }
    if (auto result = validate_quest_progress_records(progress); !result) {
        return result.error();
    }

    std::vector<QuestProgressRecord> ordered_progress(progress.begin(), progress.end());
    std::sort(ordered_progress.begin(), ordered_progress.end(),
              [](const QuestProgressRecord& left, const QuestProgressRecord& right) {
                  return left.quest_id < right.quest_id;
              });

    const fs::path directory = quest_progress_directory(save_dir);
    if (!ensure_directory(path_to_utf8(directory))) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to create quest progress directory: " + path_to_utf8(directory));
    }

    const fs::path primary_path = quest_progress_path(save_dir, player_id);
    fs::path temporary_path = primary_path;
    temporary_path += ".tmp";
    fs::path backup_path = primary_path;
    backup_path += ".bak";

    std::error_code ec;
    fs::remove(temporary_path, ec);
    if (ec) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to remove stale quest progress temporary file: " + ec.message());
    }

    {
        std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                     "Unable to create quest progress temporary file: " +
                                         path_to_utf8(temporary_path));
        }

        const uint32_t record_count = static_cast<uint32_t>(ordered_progress.size());
        file.write(kQuestProgressMagic, sizeof(kQuestProgressMagic));
        bool wrote = file.good() && write_value(file, kQuestProgressVersion) &&
                     write_quest_string(file, player_id,
                                        static_cast<uint32_t>(kMaxQuestPlayerIdBytes)) &&
                     write_value(file, record_count);
        for (const QuestProgressRecord& record : ordered_progress) {
            wrote = wrote && write_quest_progress_record(file, record);
        }
        file.flush();
        wrote = wrote && file.good();
        file.close();
        if (!wrote || file.fail()) {
            std::error_code remove_error;
            fs::remove(temporary_path, remove_error);
            return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                     "Unable to write complete quest progress file");
        }
    }

    if (auto result = replace_recoverable_player_file(
            primary_path, temporary_path, backup_path, "quest progress");
        !result) {
        std::error_code remove_error;
        fs::remove(temporary_path, remove_error);
        return result.error();
    }
    return {};
}

snt::core::Expected<std::optional<GamePlayerPersistentState>>
GameSaveManager::load_player_state(const std::string& save_dir, std::string_view player_id) {
    if (auto result = validate_player_state_request(save_dir, player_id); !result) {
        return result.error();
    }

    const fs::path primary_path = player_state_path(save_dir, player_id);
    fs::path backup_path = primary_path;
    backup_path += ".bak";

    std::error_code ec;
    const bool primary_exists = fs::exists(primary_path, ec);
    if (ec) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to inspect player state file: " + ec.message());
    }

    fs::path selected_path = primary_path;
    if (!primary_exists) {
        const bool backup_exists = fs::exists(backup_path, ec);
        if (ec) {
            return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                     "Unable to inspect player state backup: " + ec.message());
        }
        if (!backup_exists) return std::optional<GamePlayerPersistentState>{};

        selected_path = backup_path;
        SNT_LOG_WARN("Player state primary file was missing for account '%.*s'; using recovery backup",
                     static_cast<int>(player_id.size()), player_id.data());
    }

    std::ifstream file(selected_path, std::ios::binary);
    if (!file.is_open()) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to open player state file: " + path_to_utf8(selected_path));
    }

    char magic[sizeof(kPlayerStateMagic)] = {};
    uint8_t version = 0;
    std::string stored_player_id;
    GamePlayerPersistentState state;
    file.read(magic, sizeof(magic));
    if (!file.good() || std::memcmp(magic, kPlayerStateMagic, sizeof(magic)) != 0 ||
        !read_value(file, version) || version != kPlayerStateVersion ||
        !read_quest_string(file, stored_player_id, static_cast<uint32_t>(kMaxQuestPlayerIdBytes)) ||
        stored_player_id != player_id || !read_player_state_value(file, state)) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Player state file is corrupt or not the current format");
    }
    if (file.peek() != std::char_traits<char>::eof() || file.bad()) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Player state file has trailing or unreadable data");
    }
    if (auto result = validate_player_state_value(state); !result) return result.error();
    return std::optional<GamePlayerPersistentState>{std::move(state)};
}

snt::core::Expected<void> GameSaveManager::save_player_state(
    const std::string& save_dir, std::string_view player_id,
    const GamePlayerPersistentState& state) {
    if (auto result = validate_player_state_request(save_dir, player_id); !result) {
        return result.error();
    }
    if (auto result = validate_player_state_value(state); !result) return result.error();

    const fs::path directory = quest_progress_directory(save_dir);
    if (!ensure_directory(path_to_utf8(directory))) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to create player state directory: " + path_to_utf8(directory));
    }

    const fs::path primary_path = player_state_path(save_dir, player_id);
    fs::path temporary_path = primary_path;
    temporary_path += ".tmp";
    fs::path backup_path = primary_path;
    backup_path += ".bak";

    std::error_code ec;
    fs::remove(temporary_path, ec);
    if (ec) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to remove stale player state temporary file: " + ec.message());
    }

    {
        std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                     "Unable to create player state temporary file: " +
                                         path_to_utf8(temporary_path));
        }
        file.write(kPlayerStateMagic, sizeof(kPlayerStateMagic));
        bool wrote = file.good() && write_value(file, kPlayerStateVersion) &&
                     write_quest_string(file, player_id,
                                        static_cast<uint32_t>(kMaxQuestPlayerIdBytes)) &&
                     write_player_state_value(file, state);
        file.flush();
        wrote = wrote && file.good();
        file.close();
        if (!wrote || file.fail()) {
            std::error_code remove_error;
            fs::remove(temporary_path, remove_error);
            return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                     "Unable to write complete player state file");
        }
    }

    if (auto result = replace_recoverable_player_file(
            primary_path, temporary_path, backup_path, "player state");
        !result) {
        std::error_code remove_error;
        fs::remove(temporary_path, remove_error);
        return result.error();
    }
    return {};
}

// --- Per-chunk streaming persistence ---

snt::core::Expected<void> GameSaveManager::save_loaded_chunk(
    const std::string& planet_dir,
    int64_t seed,
    const std::string& dimension_id,
    const ChunkRegistry& voxel_chunks,
    const GameChunkSidecarRegistry& sidecars,
    int chunk_x, int chunk_y, int chunk_z) {
    if (planet_dir.empty() || dimension_id.empty()) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Loaded-chunk save requires a planet directory and dimension id");
    }
    if (!ensure_directory(planet_dir) || !ensure_directory(planet_dir + "/regions") ||
        !ensure_planet_header_if_missing(planet_dir, seed, dimension_id)) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to prepare the loaded-chunk save directory");
    }

    const VoxelChunk* chunk = voxel_chunks.get_chunk(
        dimension_id, chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Cannot save terrain for a non-resident chunk");
    }

    const int rx = VoxelRegionFile::to_region(chunk_x);
    const int ry = VoxelRegionFile::to_region(chunk_y);
    const int rz = VoxelRegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);

    std::vector<VoxelRegionEntry> entries;
    if (!read_existing_region(file_path, dimension_id, rx, ry, rz, entries)) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Loaded-chunk save rejected an existing corrupt region file");
    }

    try {
        VoxelRegionEntry replacement;
        replacement.local_x = lx;
        replacement.local_y = ly;
        replacement.local_z = lz;
        const GameChunkSerializer serializer;
        const GameChunk game_chunk = assemble_game_chunk(
            *chunk, sidecars.get(ChunkKey{dimension_id, chunk_x, chunk_y, chunk_z}));
        replacement.payload = serializer.serialize(dimension_id, game_chunk);

        bool replaced = false;
        for (VoxelRegionEntry& entry : entries) {
            if (entry.local_x != lx || entry.local_y != ly || entry.local_z != lz) continue;
            entry = std::move(replacement);
            replaced = true;
            break;
        }
        if (!replaced) entries.push_back(std::move(replacement));
    } catch (const std::exception& error) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Loaded-chunk serialization failed: " + std::string(error.what()));
    } catch (...) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Loaded-chunk serialization raised an unknown exception");
    }

    if (!VoxelRegionFile::write(file_path, rx, ry, rz, dimension_id, entries)) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to write the loaded-chunk region entry");
    }
    return {};
}

snt::core::Expected<bool> GameSaveManager::load_chunk_terrain(
    const std::string& planet_dir,
    const std::string& dimension_id,
    ChunkRegistry& voxel_chunks,
    int chunk_x, int chunk_y, int chunk_z) {
    if (planet_dir.empty() || dimension_id.empty()) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Terrain load requires a planet directory and dimension id");
    }
    if (voxel_chunks.has_chunk(dimension_id, chunk_x, chunk_y, chunk_z)) return true;

    const int rx = VoxelRegionFile::to_region(chunk_x);
    const int ry = VoxelRegionFile::to_region(chunk_y);
    const int rz = VoxelRegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);
    const fs::path region_path = utf8_path(file_path);

    std::error_code exists_error;
    const bool region_exists = fs::exists(region_path, exists_error);
    if (exists_error) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to inspect terrain region file: " + exists_error.message());
    }
    if (!region_exists) return false;

    std::vector<VoxelRegionEntry> entries;
    std::string file_dimension_id;
    int file_rx = 0;
    int file_ry = 0;
    int file_rz = 0;
    if (!VoxelRegionFile::read(file_path, file_dimension_id, file_rx, file_ry, file_rz, entries) ||
        file_dimension_id != dimension_id || file_rx != rx || file_ry != ry || file_rz != rz) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Terrain load rejected a corrupt or mismatched region file");
    }

    for (const VoxelRegionEntry& entry : entries) {
        if (entry.local_x != lx || entry.local_y != ly || entry.local_z != lz) continue;
        std::string payload_dimension_id;
        GameChunk chunk;
        const GameChunkSerializer serializer;
        if (!serializer.deserialize(entry.payload, payload_dimension_id, chunk) ||
            payload_dimension_id != dimension_id ||
            chunk.chunk_x != chunk_x || chunk.chunk_y != chunk_y || chunk.chunk_z != chunk_z) {
            return persistence_error(snt::core::ErrorCode::kInvalidState,
                                     "Terrain load rejected a corrupt or mismatched chunk payload");
        }
        voxel_chunks.set_chunk(dimension_id, chunk_x, chunk_y, chunk_z,
                               std::move(chunk.voxel_chunk()));
        return true;
    }
    return false;
}

snt::core::Expected<void> GameSaveManager::save_chunk_sidecar(
    const std::string& planet_dir,
    int64_t seed,
    const std::string& dimension_id,
    int chunk_x, int chunk_y, int chunk_z,
    const GameChunkSidecar& sidecar) {
    if (planet_dir.empty() || dimension_id.empty()) {
        return persistence_error(snt::core::ErrorCode::kInvalidArgument,
                                 "Sidecar save requires a planet directory and dimension id");
    }
    if (!ensure_directory(planet_dir) || !ensure_directory(planet_dir + "/regions") ||
        !ensure_planet_header_if_missing(planet_dir, seed, dimension_id)) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to prepare the sidecar save directory");
    }

    const int rx = VoxelRegionFile::to_region(chunk_x);
    const int ry = VoxelRegionFile::to_region(chunk_y);
    const int rz = VoxelRegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);
    const fs::path region_path = utf8_path(file_path);

    std::error_code exists_error;
    const bool region_exists = fs::exists(region_path, exists_error);
    if (exists_error) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to inspect sidecar region file: " + exists_error.message());
    }
    if (!region_exists) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Cannot update a sidecar without its persisted terrain chunk");
    }

    std::vector<VoxelRegionEntry> entries;
    std::string file_dimension_id;
    int file_rx = 0;
    int file_ry = 0;
    int file_rz = 0;
    if (!VoxelRegionFile::read(file_path, file_dimension_id, file_rx, file_ry, file_rz, entries) ||
        file_dimension_id != dimension_id || file_rx != rx || file_ry != ry || file_rz != rz) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Sidecar save rejected a corrupt or mismatched region file");
    }

    bool replaced = false;
    const GameChunkSerializer serializer;
    for (VoxelRegionEntry& entry : entries) {
        if (entry.local_x != lx || entry.local_y != ly || entry.local_z != lz) continue;
        std::string payload_dimension_id;
        GameChunk chunk;
        if (!serializer.deserialize(entry.payload, payload_dimension_id, chunk) ||
            payload_dimension_id != dimension_id ||
            chunk.chunk_x != chunk_x || chunk.chunk_y != chunk_y || chunk.chunk_z != chunk_z) {
            return persistence_error(snt::core::ErrorCode::kInvalidState,
                                     "Sidecar save rejected a corrupt or mismatched chunk payload");
        }
        try {
            chunk.sidecar() = sidecar;
            entry.payload = serializer.serialize(dimension_id, chunk);
        } catch (const std::exception& error) {
            return persistence_error(snt::core::ErrorCode::kInvalidState,
                                     "Sidecar serialization failed: " + std::string(error.what()));
        } catch (...) {
            return persistence_error(snt::core::ErrorCode::kInvalidState,
                                     "Sidecar serialization raised an unknown exception");
        }
        replaced = true;
        break;
    }
    if (!replaced) {
        return persistence_error(snt::core::ErrorCode::kInvalidState,
                                 "Cannot update a sidecar for a missing persisted chunk");
    }
    if (!VoxelRegionFile::write(file_path, rx, ry, rz, dimension_id, entries)) {
        return persistence_error(snt::core::ErrorCode::kFileOpenFailed,
                                 "Unable to write the sidecar region entry");
    }
    return {};
}

bool GameSaveManager::delete_chunk(const std::string& planet_dir,
                               const std::string& dimension_id,
                               int chunk_x, int chunk_y, int chunk_z) {
    const int rx = VoxelRegionFile::to_region(chunk_x);
    const int ry = VoxelRegionFile::to_region(chunk_y);
    const int rz = VoxelRegionFile::to_region(chunk_z);
    const uint8_t lx = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_x));
    const uint8_t ly = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_y));
    const uint8_t lz = static_cast<uint8_t>(VoxelRegionFile::to_local(chunk_z));
    const std::string file_path = region_file_path(planet_dir, dimension_id, rx, ry, rz);

    const fs::path path = utf8_path(file_path);
    if (!fs::exists(path)) {
        return true;
    }

    std::vector<VoxelRegionEntry> entries;
    if (!read_existing_region(file_path, dimension_id, rx, ry, rz, entries)) {
        return false;
    }

    const auto old_size = entries.size();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
            [lx, ly, lz](const VoxelRegionEntry& entry) {
                return entry.local_x == lx && entry.local_y == ly && entry.local_z == lz;
            }),
        entries.end());

    if (entries.size() == old_size) {
        return true;
    }

    if (entries.empty()) {
        std::error_code ec;
        fs::remove(path, ec);
        return !ec;
    }

    return VoxelRegionFile::write(file_path, rx, ry, rz, dimension_id, entries);
}

// --- Planet data (header + summary) ---

void GameSaveManager::write_string(std::ofstream& file, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    file.write(reinterpret_cast<const char*>(&len), sizeof(len));
    if (len > 0) {
        file.write(str.data(), len);
    }
}

bool GameSaveManager::read_string(std::ifstream& file, std::string& out,
                               uint32_t max_len) {
    uint32_t len;
    file.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (!file.good() || len > max_len) {
        return false;
    }
    out.resize(len);
    if (len > 0) {
        file.read(out.data(), len);
    }
    return file.good();
}

bool GameSaveManager::write_planet_data(const std::string& planet_dir,
                                     int64_t seed,
                                     const std::string& dimension_id,
                                     const GamePlanetSummaryData* summary) {
    if (!ensure_directory(planet_dir)) {
        return false;
    }

    std::string path = planet_dir + "/planet_data.bin";
    std::ofstream file(utf8_path(path), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Header section.
    uint8_t version = kPlanetDataVersion;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&seed), sizeof(seed));
    write_string(file, dimension_id);

    // Summary section.
    uint8_t has_summary = (summary != nullptr) ? 1 : 0;
    file.write(reinterpret_cast<const char*>(&has_summary), sizeof(has_summary));

    if (summary != nullptr) {
        file.write(reinterpret_cast<const char*>(&summary->captured_tick),
                   sizeof(summary->captured_tick));

        // Production lines.
        uint32_t pl_count = static_cast<uint32_t>(summary->production_lines.size());
        file.write(reinterpret_cast<const char*>(&pl_count), sizeof(pl_count));
        for (const auto& line : summary->production_lines) {
            write_string(file, line.recipe_key);
            file.write(reinterpret_cast<const char*>(&line.rate_per_minute),
                       sizeof(line.rate_per_minute));
            file.write(reinterpret_cast<const char*>(&line.active_count),
                       sizeof(line.active_count));
        }

        // Mining sites.
        uint32_t ms_count = static_cast<uint32_t>(summary->mining_sites.size());
        file.write(reinterpret_cast<const char*>(&ms_count), sizeof(ms_count));
        for (const auto& site : summary->mining_sites) {
            write_string(file, site.ore_key);
            file.write(reinterpret_cast<const char*>(&site.rate_per_minute),
                       sizeof(site.rate_per_minute));
            file.write(reinterpret_cast<const char*>(&site.remaining_approx),
                       sizeof(site.remaining_approx));
        }

        // Storage levels.
        uint32_t sl_count = static_cast<uint32_t>(summary->storage_levels.size());
        file.write(reinterpret_cast<const char*>(&sl_count), sizeof(sl_count));
        for (const auto& entry : summary->storage_levels) {
            write_string(file, entry.item_key);
            file.write(reinterpret_cast<const char*>(&entry.count),
                       sizeof(entry.count));
            file.write(reinterpret_cast<const char*>(&entry.capacity),
                       sizeof(entry.capacity));
        }

        // Power summary.
        file.write(reinterpret_cast<const char*>(&summary->power_consumption_mw),
                   sizeof(summary->power_consumption_mw));
        file.write(reinterpret_cast<const char*>(&summary->power_generation_mw),
                   sizeof(summary->power_generation_mw));
        file.write(reinterpret_cast<const char*>(&summary->power_surplus_mw),
                   sizeof(summary->power_surplus_mw));

        // Accumulated production.
        uint32_t ap_count = static_cast<uint32_t>(summary->accumulated_production.size());
        file.write(reinterpret_cast<const char*>(&ap_count), sizeof(ap_count));
        for (const auto& entry : summary->accumulated_production) {
            write_string(file, entry.item_key);
            file.write(reinterpret_cast<const char*>(&entry.amount),
                       sizeof(entry.amount));
        }

        // Accumulated consumption.
        uint32_t ac_count = static_cast<uint32_t>(summary->accumulated_consumption.size());
        file.write(reinterpret_cast<const char*>(&ac_count), sizeof(ac_count));
        for (const auto& entry : summary->accumulated_consumption) {
            write_string(file, entry.item_key);
            file.write(reinterpret_cast<const char*>(&entry.amount),
                       sizeof(entry.amount));
        }
    }

    file.close();
    return file.good();
}

bool GameSaveManager::read_planet_data(const std::string& planet_dir,
                                    int64_t& out_seed,
                                    std::string& out_dimension_id,
                                    GamePlanetSummaryData& out_summary,
                                    bool& out_has_summary) {
    std::string path = planet_dir + "/planet_data.bin";

    std::ifstream file(utf8_path(path), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    uint8_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good()) {
        return false;
    }

    if (version != kPlanetDataVersion) {
        return false;
    }

    // Read seed.
    file.read(reinterpret_cast<char*>(&out_seed), sizeof(out_seed));
    if (!file.good()) {
        return false;
    }

    // Read dimension_id.
    if (!read_string(file, out_dimension_id)) {
        return false;
    }

    // Read summary section.
    out_has_summary = false;
    uint8_t has_summary;
    file.read(reinterpret_cast<char*>(&has_summary), sizeof(has_summary));
    if (!file.good()) {
        return true;  // Header is valid, just no summary.
    }

    out_has_summary = (has_summary == 1);
    if (out_has_summary) {
        file.read(reinterpret_cast<char*>(&out_summary.captured_tick),
                  sizeof(out_summary.captured_tick));
        if (!file.good()) return false;

        // Production lines.
        uint32_t pl_count;
        file.read(reinterpret_cast<char*>(&pl_count), sizeof(pl_count));
        if (!file.good()) return false;
        out_summary.production_lines.resize(pl_count);
        for (uint32_t i = 0; i < pl_count; ++i) {
            if (!read_string(file, out_summary.production_lines[i].recipe_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.production_lines[i].rate_per_minute),
                      sizeof(float));
            if (!file.good()) return false;
            file.read(reinterpret_cast<char*>(&out_summary.production_lines[i].active_count),
                      sizeof(int32_t));
            if (!file.good()) return false;
        }

        // Mining sites.
        uint32_t ms_count;
        file.read(reinterpret_cast<char*>(&ms_count), sizeof(ms_count));
        if (!file.good()) return false;
        out_summary.mining_sites.resize(ms_count);
        for (uint32_t i = 0; i < ms_count; ++i) {
            if (!read_string(file, out_summary.mining_sites[i].ore_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.mining_sites[i].rate_per_minute),
                      sizeof(float));
            if (!file.good()) return false;
            file.read(reinterpret_cast<char*>(&out_summary.mining_sites[i].remaining_approx),
                      sizeof(int64_t));
            if (!file.good()) return false;
        }

        // Storage levels.
        uint32_t sl_count;
        file.read(reinterpret_cast<char*>(&sl_count), sizeof(sl_count));
        if (!file.good()) return false;
        out_summary.storage_levels.resize(sl_count);
        for (uint32_t i = 0; i < sl_count; ++i) {
            if (!read_string(file, out_summary.storage_levels[i].item_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.storage_levels[i].count),
                      sizeof(int32_t));
            if (!file.good()) return false;
            file.read(reinterpret_cast<char*>(&out_summary.storage_levels[i].capacity),
                      sizeof(int32_t));
            if (!file.good()) return false;
        }

        // Power summary.
        file.read(reinterpret_cast<char*>(&out_summary.power_consumption_mw), sizeof(float));
        if (!file.good()) return false;
        file.read(reinterpret_cast<char*>(&out_summary.power_generation_mw), sizeof(float));
        if (!file.good()) return false;
        file.read(reinterpret_cast<char*>(&out_summary.power_surplus_mw), sizeof(float));
        if (!file.good()) return false;

        // Accumulated production.
        uint32_t ap_count;
        file.read(reinterpret_cast<char*>(&ap_count), sizeof(ap_count));
        if (!file.good()) return false;
        out_summary.accumulated_production.resize(ap_count);
        for (uint32_t i = 0; i < ap_count; ++i) {
            if (!read_string(file, out_summary.accumulated_production[i].item_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.accumulated_production[i].amount),
                      sizeof(double));
            if (!file.good()) return false;
        }

        // Accumulated consumption.
        uint32_t ac_count;
        file.read(reinterpret_cast<char*>(&ac_count), sizeof(ac_count));
        if (!file.good()) return false;
        out_summary.accumulated_consumption.resize(ac_count);
        for (uint32_t i = 0; i < ac_count; ++i) {
            if (!read_string(file, out_summary.accumulated_consumption[i].item_key)) return false;
            file.read(reinterpret_cast<char*>(&out_summary.accumulated_consumption[i].amount),
                      sizeof(double));
            if (!file.good()) return false;
        }
    }

    return true;
}

bool GameSaveManager::write_universe_header(const std::string& save_dir,
                                        int64_t seed,
                                        const std::string& universe_mode) {
    if (!ensure_directory(save_dir)) {
        return false;
    }

    std::string header_path = save_dir + "/universe_header.bin";
    std::ofstream file(utf8_path(header_path), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    uint8_t version = kUniverseHeaderVersion;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&seed), sizeof(seed));
    write_string(file, universe_mode);

    file.close();
    return file.good();
}

std::tuple<bool, int64_t, std::string> GameSaveManager::read_universe_header(
        const std::string& save_dir) {
    std::string header_path = save_dir + "/universe_header.bin";

    std::ifstream file(utf8_path(header_path), std::ios::binary);
    if (!file.is_open()) {
        return {false, 0, ""};
    }

    uint8_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file.good() || version != kUniverseHeaderVersion) {
        return {false, 0, ""};
    }

    int64_t seed;
    file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
    if (!file.good()) {
        return {false, 0, ""};
    }

    std::string universe_mode;
    if (!read_string(file, universe_mode)) {
        return {false, 0, ""};
    }

    return {true, seed, universe_mode};
}

// --- Utility ---

std::vector<std::string> GameSaveManager::list_planets(
        const std::string& save_dir) {
    std::vector<std::string> result;

    std::string planets_dir = save_dir + "/planets";
    const fs::path planets_path = utf8_path(planets_dir);
    if (!fs::exists(planets_path) || !fs::is_directory(planets_path)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(planets_path)) {
        if (!entry.is_directory()) {
            continue;
        }

        // Only accept planet_data.bin (v2 format).
        std::string data_path =
            path_to_utf8(entry.path()) + "/planet_data.bin";
        if (fs::exists(utf8_path(data_path)) &&
            fs::is_regular_file(utf8_path(data_path))) {
            result.push_back(path_to_utf8(entry.path().filename()));
        }
    }

    return result;
}

std::string GameSaveManager::planet_dir(const std::string& save_dir,
                                    const std::string& dimension_id) {
    return save_dir + "/planets/" + dimension_id;
}

} // namespace science_and_theology
