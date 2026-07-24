// Crash-recovery journal codec for cross-file ground-loot pickup commits.

#define SNT_LOG_CHANNEL "game.ground_loot_journal"
#include "game/world/save/ground_loot_pickup_journal.h"

#include "core/error.h"
#include "core/log.h"
#include "voxel/data/voxel_chunk.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace snt::game {
namespace {

namespace fs = std::filesystem;

constexpr char kGroundLootPickupJournalMagic[] = {'S', 'N', 'T', 'L'};
constexpr uint8_t kGroundLootPickupJournalVersion = 2;
constexpr size_t kMaxAccountIdBytes = 256;
constexpr size_t kMaxDimensionIdBytes = 128;
constexpr size_t kMaxResourceTypeBytes = 64;
constexpr size_t kMaxResourceIdBytes = 256;
constexpr size_t kMaxResourceVariantBytes = 16u * 1024u;
constexpr uint32_t kMaxGroundLootPickupClaims = 4096;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error file_error(std::string message) {
    return {snt::core::ErrorCode::kFileOpenFailed, std::move(message)};
}

[[nodiscard]] fs::path utf8_path(std::string_view path) {
    return fs::u8path(path);
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

[[nodiscard]] fs::path journal_path(std::string_view universe_save_dir) {
    return utf8_path(universe_save_dir) / "ground_loot_pickups.bin";
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

bool write_string(std::ofstream& file, std::string_view value, size_t max_length) {
    if (value.size() > max_length || value.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    const uint32_t length = static_cast<uint32_t>(value.size());
    if (!write_value(file, length)) return false;
    if (length == 0) return true;
    file.write(value.data(), static_cast<std::streamsize>(length));
    return file.good();
}

bool read_string(std::ifstream& file, std::string& value, size_t max_length) {
    uint32_t length = 0;
    if (!read_value(file, length) || length > max_length) return false;
    value.resize(length);
    if (length == 0) return true;
    file.read(value.data(), static_cast<std::streamsize>(length));
    return file.good();
}

[[nodiscard]] int32_t floor_divide(int32_t value, int32_t divisor) noexcept {
    const int32_t quotient = value / divisor;
    const int32_t remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] bool is_finite_chunk_position(float value) noexcept {
    return std::isfinite(value) &&
           value >= static_cast<float>(std::numeric_limits<int32_t>::min() + 1) &&
           value <= static_cast<float>(std::numeric_limits<int32_t>::max() - 1);
}

[[nodiscard]] bool claim_matches_chunk(const GameGroundLootPickupClaim& claim) noexcept {
    if (!is_finite_chunk_position(claim.record.position_x) ||
        !is_finite_chunk_position(claim.record.position_y) ||
        !is_finite_chunk_position(claim.record.position_z)) {
        return false;
    }
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    return floor_divide(static_cast<int32_t>(std::floor(claim.record.position_x)), kChunkSize) ==
               claim.chunk.chunk_x &&
           floor_divide(static_cast<int32_t>(std::floor(claim.record.position_y)), kChunkSize) ==
               claim.chunk.chunk_y &&
           floor_divide(static_cast<int32_t>(std::floor(claim.record.position_z)), kChunkSize) ==
               claim.chunk.chunk_z;
}

[[nodiscard]] bool has_valid_claim_shape(const GameGroundLootPickupClaim& claim) noexcept {
    const ResourceContentStack& resource = claim.record.resource;
    return claim.loot_id != 0 && claim.record.loot_id == claim.loot_id &&
           !claim.account_id.empty() && claim.account_id.size() <= kMaxAccountIdBytes &&
           claim.account_id.find('\0') == std::string::npos &&
           !claim.chunk.dimension_id.empty() &&
           claim.chunk.dimension_id.size() <= kMaxDimensionIdBytes &&
           claim.chunk.dimension_id.find('\0') == std::string::npos &&
           resource.is_valid() && resource.is_item() &&
           resource.key.type.size() <= kMaxResourceTypeBytes &&
           resource.key.id.size() <= kMaxResourceIdBytes &&
           resource.key.variant.size() <= kMaxResourceVariantBytes && claim_matches_chunk(claim);
}

bool write_claim(std::ofstream& file, const GameGroundLootPickupClaim& claim) {
    const ResourceContentStack& resource = claim.record.resource;
    return write_value(file, claim.loot_id) &&
           write_string(file, claim.account_id, kMaxAccountIdBytes) &&
           write_string(file, claim.chunk.dimension_id, kMaxDimensionIdBytes) &&
           write_value(file, claim.chunk.chunk_x) && write_value(file, claim.chunk.chunk_y) &&
           write_value(file, claim.chunk.chunk_z) &&
           write_string(file, resource.key.type, kMaxResourceTypeBytes) &&
           write_string(file, resource.key.id, kMaxResourceIdBytes) &&
           write_string(file, resource.key.variant, kMaxResourceVariantBytes) &&
           write_value(file, resource.amount) && write_value(file, claim.record.position_x) &&
           write_value(file, claim.record.position_y) && write_value(file, claim.record.position_z) &&
           write_value(file, claim.record.spawned_tick) &&
           write_value(file, claim.record.lifetime_ticks);
}

bool read_claim(std::ifstream& file, GameGroundLootPickupClaim& claim) {
    GameGroundLootPickupClaim restored;
    if (!read_value(file, restored.loot_id) ||
        !read_string(file, restored.account_id, kMaxAccountIdBytes) ||
        !read_string(file, restored.chunk.dimension_id, kMaxDimensionIdBytes) ||
        !read_value(file, restored.chunk.chunk_x) || !read_value(file, restored.chunk.chunk_y) ||
        !read_value(file, restored.chunk.chunk_z) ||
        !read_string(file, restored.record.resource.key.type, kMaxResourceTypeBytes) ||
        !read_string(file, restored.record.resource.key.id, kMaxResourceIdBytes) ||
        !read_string(file, restored.record.resource.key.variant, kMaxResourceVariantBytes) ||
        !read_value(file, restored.record.resource.amount) ||
        !read_value(file, restored.record.position_x) || !read_value(file, restored.record.position_y) ||
        !read_value(file, restored.record.position_z) || !read_value(file, restored.record.spawned_tick)) {
        return false;
    }
    if (!read_value(file, restored.record.lifetime_ticks)) {
        return false;
    }
    restored.record.loot_id = restored.loot_id;
    if (!has_valid_claim_shape(restored)) return false;
    claim = std::move(restored);
    return true;
}

snt::core::Expected<void> replace_recoverable_journal_file(
    const fs::path& primary_path, const fs::path& temporary_path, const fs::path& backup_path) {
    std::error_code error;
    const bool has_primary = fs::exists(primary_path, error);
    if (error) return file_error("Unable to inspect ground loot journal primary: " + error.message());
    const bool has_backup = fs::exists(backup_path, error);
    if (error) return file_error("Unable to inspect ground loot journal backup: " + error.message());

    if (!has_primary && has_backup) {
        fs::rename(backup_path, primary_path, error);
        if (error) return file_error("Unable to restore ground loot journal backup: " + error.message());
    } else if (has_primary && has_backup) {
        fs::remove(backup_path, error);
        if (error) return file_error("Unable to remove stale ground loot journal backup: " + error.message());
    }

    const bool primary_exists = fs::exists(primary_path, error);
    if (error) return file_error("Unable to inspect ground loot journal destination: " + error.message());
    if (primary_exists) {
        fs::rename(primary_path, backup_path, error);
        if (error) return file_error("Unable to stage previous ground loot journal: " + error.message());
    }

    fs::rename(temporary_path, primary_path, error);
    if (!error) {
        fs::remove(backup_path, error);
        return {};
    }

    const std::string rename_error = error.message();
    std::error_code restore_error;
    if (!fs::exists(primary_path, restore_error) && fs::exists(backup_path, restore_error) &&
        !restore_error) {
        fs::rename(backup_path, primary_path, restore_error);
    }
    return file_error("Unable to promote ground loot journal: " + rename_error);
}

}  // namespace

snt::core::Expected<std::vector<GameGroundLootPickupClaim>> GameGroundLootPickupJournal::load(
    std::string_view universe_save_dir) {
    if (universe_save_dir.empty()) {
        return invalid_argument("Ground loot journal save directory must not be empty");
    }

    const fs::path primary_path = journal_path(universe_save_dir);
    fs::path backup_path = primary_path;
    backup_path += ".bak";
    std::error_code error;
    const bool primary_exists = fs::exists(primary_path, error);
    if (error) return file_error("Unable to inspect ground loot journal: " + error.message());
    fs::path selected_path = primary_path;
    if (!primary_exists) {
        const bool backup_exists = fs::exists(backup_path, error);
        if (error) return file_error("Unable to inspect ground loot journal backup: " + error.message());
        if (!backup_exists) return std::vector<GameGroundLootPickupClaim>{};
        selected_path = backup_path;
        SNT_LOG_WARN("Ground loot pickup journal primary was missing; using recovery backup");
    }

    std::ifstream file(selected_path, std::ios::binary);
    if (!file.is_open()) {
        return file_error("Unable to open ground loot journal: " + path_to_utf8(selected_path));
    }
    char magic[sizeof(kGroundLootPickupJournalMagic)] = {};
    uint8_t version = 0;
    uint32_t claim_count = 0;
    file.read(magic, sizeof(magic));
    if (!file.good() ||
        std::memcmp(magic, kGroundLootPickupJournalMagic, sizeof(magic)) != 0 ||
        !read_value(file, version) || version != kGroundLootPickupJournalVersion ||
        !read_value(file, claim_count) || claim_count > kMaxGroundLootPickupClaims) {
        return invalid_argument("Ground loot journal is corrupt or not the current format");
    }

    std::vector<GameGroundLootPickupClaim> claims;
    claims.reserve(claim_count);
    std::set<uint64_t> seen_ids;
    for (uint32_t index = 0; index < claim_count; ++index) {
        GameGroundLootPickupClaim claim;
        if (!read_claim(file, claim) || !seen_ids.insert(claim.loot_id).second) {
            return invalid_argument("Ground loot journal contains an invalid or duplicate claim");
        }
        claims.push_back(std::move(claim));
    }
    if (file.peek() != std::char_traits<char>::eof() || file.bad()) {
        return invalid_argument("Ground loot journal has trailing or unreadable data");
    }
    return claims;
}

snt::core::Expected<void> GameGroundLootPickupJournal::save(
    std::string_view universe_save_dir, std::span<const GameGroundLootPickupClaim> claims) {
    if (universe_save_dir.empty()) {
        return invalid_argument("Ground loot journal save directory must not be empty");
    }
    if (claims.size() > kMaxGroundLootPickupClaims) {
        return invalid_argument("Ground loot journal contains too many claims");
    }
    std::set<uint64_t> seen_ids;
    for (const GameGroundLootPickupClaim& claim : claims) {
        if (!has_valid_claim_shape(claim) || !seen_ids.insert(claim.loot_id).second) {
            return invalid_argument("Ground loot journal contains an invalid or duplicate claim");
        }
    }

    const fs::path directory = utf8_path(universe_save_dir);
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) return file_error("Unable to create ground loot journal directory: " + error.message());

    const fs::path primary_path = journal_path(universe_save_dir);
    fs::path temporary_path = primary_path;
    temporary_path += ".tmp";
    fs::path backup_path = primary_path;
    backup_path += ".bak";
    fs::remove(temporary_path, error);
    if (error) return file_error("Unable to remove stale ground loot journal temporary file: " + error.message());

    {
        std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return file_error("Unable to create ground loot journal: " + path_to_utf8(temporary_path));
        }
        const uint32_t claim_count = static_cast<uint32_t>(claims.size());
        file.write(kGroundLootPickupJournalMagic, sizeof(kGroundLootPickupJournalMagic));
        bool wrote = file.good() && write_value(file, kGroundLootPickupJournalVersion) &&
                     write_value(file, claim_count);
        for (const GameGroundLootPickupClaim& claim : claims) {
            wrote = wrote && write_claim(file, claim);
        }
        file.flush();
        wrote = wrote && file.good();
        file.close();
        if (!wrote || file.fail()) {
            std::error_code remove_error;
            fs::remove(temporary_path, remove_error);
            return file_error("Unable to write complete ground loot journal");
        }
    }

    if (auto result = replace_recoverable_journal_file(
            primary_path, temporary_path, backup_path);
        !result) {
        std::error_code remove_error;
        fs::remove(temporary_path, remove_error);
        return result.error();
    }
    return {};
}

}  // namespace snt::game
