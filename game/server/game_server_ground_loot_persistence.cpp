// Dedicated-server recovery coordinator for ground-loot pickup journals.

#define SNT_LOG_CHANNEL "game.server_ground_loot_persistence"
#include "game/server/game_server_ground_loot_persistence.h"

#include "game/client/game_content_registry.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/world/save/save_manager.h"
#include "game/world/save/world_persistence_lifecycle.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include "core/error.h"
#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] int32_t floor_divide(int32_t value, int32_t divisor) noexcept {
    const int32_t quotient = value / divisor;
    const int32_t remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] bool claim_matches_chunk(const GameGroundLootPickupClaim& claim) noexcept {
    const GameGroundLootRecord& record = claim.record;
    if (!std::isfinite(record.position_x) || !std::isfinite(record.position_y) ||
        !std::isfinite(record.position_z) ||
        record.position_x < static_cast<float>(std::numeric_limits<int32_t>::min() + 1) ||
        record.position_x > static_cast<float>(std::numeric_limits<int32_t>::max() - 1) ||
        record.position_y < static_cast<float>(std::numeric_limits<int32_t>::min() + 1) ||
        record.position_y > static_cast<float>(std::numeric_limits<int32_t>::max() - 1) ||
        record.position_z < static_cast<float>(std::numeric_limits<int32_t>::min() + 1) ||
        record.position_z > static_cast<float>(std::numeric_limits<int32_t>::max() - 1)) {
        return false;
    }
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    return floor_divide(static_cast<int32_t>(std::floor(record.position_x)), kChunkSize) ==
               claim.chunk.chunk_x &&
           floor_divide(static_cast<int32_t>(std::floor(record.position_y)), kChunkSize) ==
               claim.chunk.chunk_y &&
           floor_divide(static_cast<int32_t>(std::floor(record.position_z)), kChunkSize) ==
               claim.chunk.chunk_z;
}

[[nodiscard]] bool same_record(const GameGroundLootRecord& left,
                               const GameGroundLootRecord& right) noexcept {
    return left.loot_id == right.loot_id && left.resource == right.resource &&
           left.position_x == right.position_x && left.position_y == right.position_y &&
           left.position_z == right.position_z && left.spawned_tick == right.spawned_tick &&
           left.lifetime_ticks == right.lifetime_ticks;
}

[[nodiscard]] bool contains_receipt(const GamePlayerPersistentState& state,
                                    uint64_t claim_id) noexcept {
    return std::find(state.ground_loot_claim_receipts.begin(),
                     state.ground_loot_claim_receipts.end(), claim_id) !=
           state.ground_loot_claim_receipts.end();
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerGroundLootPickupPersistence>>
GameServerGroundLootPickupPersistence::create(
    std::string universe_save_dir, GameServerPlayerLifecycle& player_lifecycle,
    GameWorldPersistenceLifecycle& world_persistence, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content) {
    if (universe_save_dir.empty()) {
        return invalid_argument("Ground loot pickup persistence requires a universe save directory");
    }
    return std::unique_ptr<GameServerGroundLootPickupPersistence>(
        new GameServerGroundLootPickupPersistence(
            std::move(universe_save_dir), player_lifecycle, world_persistence, chunks, sidecars,
            content));
}

GameServerGroundLootPickupPersistence::GameServerGroundLootPickupPersistence(
    std::string universe_save_dir, GameServerPlayerLifecycle& player_lifecycle,
    GameWorldPersistenceLifecycle& world_persistence, snt::voxel::ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content) noexcept
    : universe_save_dir_(std::move(universe_save_dir)), player_lifecycle_(&player_lifecycle),
      world_persistence_(&world_persistence), chunks_(&chunks), sidecars_(&sidecars),
      content_(&content) {}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::validate_claim(
    const GameGroundLootPickupClaim& claim) const {
    if (content_ == nullptr || claim.loot_id == 0 || claim.record.loot_id != claim.loot_id ||
        claim.account_id.empty() || claim.account_id.find('\0') != std::string::npos ||
        claim.chunk.dimension_id.empty() || claim.chunk.dimension_id.find('\0') != std::string::npos ||
        !claim_matches_chunk(claim) || !claim.record.resource.is_valid() ||
        !claim.record.resource.is_item() ||
        content_->find_item(claim.record.resource.key.id) == nullptr) {
        return invalid_argument("Ground loot pickup journal claim is invalid");
    }
    if (world_persistence_ == nullptr ||
        claim.chunk.dimension_id != world_persistence_->descriptor().dimension_id) {
        return invalid_argument("Ground loot pickup claim belongs to an unmanaged dimension");
    }
    return {};
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::persist_claims() {
    std::vector<GameGroundLootPickupClaim> claims;
    claims.reserve(claims_.size());
    for (const auto& [loot_id, claim] : claims_) {
        static_cast<void>(loot_id);
        claims.push_back(claim);
    }
    return GameGroundLootPickupJournal::save(universe_save_dir_, claims);
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::persist_chunk(
    const ChunkKey& chunk) {
    if (world_persistence_ == nullptr || chunks_ == nullptr || sidecars_ == nullptr) {
        return invalid_state("Ground loot pickup persistence has no world checkpoint services");
    }
    auto result = world_persistence_->checkpoint_chunk(*chunks_, *sidecars_, chunk);
    if (!result) {
        auto error = result.error();
        error.with_context("GameServerGroundLootPickupPersistence::persist_chunk");
        return error;
    }
    return {};
}

snt::core::Expected<bool> GameServerGroundLootPickupPersistence::player_has_durable_receipt(
    const GameGroundLootPickupClaim& claim) const {
    auto player = GameSaveManager::load_player_state(universe_save_dir_, claim.account_id);
    if (!player) {
        auto error = player.error();
        error.with_context("GameServerGroundLootPickupPersistence::player_has_durable_receipt");
        return error;
    }
    return player->has_value() && contains_receipt(**player, claim.loot_id);
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::restore_world_record(
    const GameGroundLootPickupClaim& claim) {
    if (sidecars_ == nullptr) {
        return invalid_state("Ground loot pickup persistence has no sidecar registry");
    }
    GameChunkSidecar* sidecar = sidecars_->get(claim.chunk);
    if (sidecar == nullptr) {
        sidecars_->set(claim.chunk, {});
        sidecar = sidecars_->get(claim.chunk);
    }
    if (sidecar == nullptr) {
        return invalid_state("Ground loot pickup recovery could not create the source sidecar");
    }
    const auto existing = std::find_if(
        sidecar->ground_loot.begin(), sidecar->ground_loot.end(),
        [&claim](const GameGroundLootRecord& record) { return record.loot_id == claim.loot_id; });
    if (existing != sidecar->ground_loot.end()) {
        if (!same_record(*existing, claim.record)) {
            return invalid_state("Ground loot pickup recovery found a conflicting loot id");
        }
    } else {
        const auto insertion = std::lower_bound(
            sidecar->ground_loot.begin(), sidecar->ground_loot.end(), claim.loot_id,
            [](const GameGroundLootRecord& record, uint64_t loot_id) {
                return record.loot_id < loot_id;
            });
        sidecar->ground_loot.insert(insertion, claim.record);
    }
    if (claim.loot_id == std::numeric_limits<uint64_t>::max()) {
        return invalid_state("Ground loot pickup recovery cannot restore the maximum loot id");
    }
    sidecar->next_ground_loot_serial = std::max(
        sidecar->next_ground_loot_serial, claim.loot_id + 1);
    return persist_chunk(claim.chunk);
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::remove_world_record(
    const GameGroundLootPickupClaim& claim) {
    if (sidecars_ == nullptr) {
        return invalid_state("Ground loot pickup persistence has no sidecar registry");
    }
    GameChunkSidecar* sidecar = sidecars_->get(claim.chunk);
    if (sidecar == nullptr) {
        sidecars_->set(claim.chunk, {});
        sidecar = sidecars_->get(claim.chunk);
    }
    if (sidecar == nullptr) {
        return invalid_state("Ground loot pickup recovery could not create the source sidecar");
    }
    const auto existing = std::find_if(
        sidecar->ground_loot.begin(), sidecar->ground_loot.end(),
        [&claim](const GameGroundLootRecord& record) { return record.loot_id == claim.loot_id; });
    if (existing != sidecar->ground_loot.end()) {
        if (!same_record(*existing, claim.record)) {
            return invalid_state("Ground loot pickup recovery found a conflicting loot id");
        }
        sidecar->ground_loot.erase(existing);
    }
    return persist_chunk(claim.chunk);
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::erase_claim(uint64_t loot_id) {
    if (claims_.erase(loot_id) != 1) {
        return invalid_state("Ground loot pickup journal lost its claim before completion");
    }
    return persist_claims();
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::recover() {
    auto loaded = GameGroundLootPickupJournal::load(universe_save_dir_);
    if (!loaded) {
        auto error = loaded.error();
        error.with_context("GameServerGroundLootPickupPersistence::recover(load journal)");
        return error;
    }

    claims_.clear();
    for (GameGroundLootPickupClaim& claim : *loaded) {
        if (auto result = validate_claim(claim); !result) return result.error();
        if (!claims_.emplace(claim.loot_id, std::move(claim)).second) {
            return invalid_state("Ground loot pickup journal has duplicate claim ids");
        }
    }
    recovered_ = true;

    std::vector<uint64_t> pending_ids;
    pending_ids.reserve(claims_.size());
    for (const auto& [loot_id, claim] : claims_) {
        static_cast<void>(claim);
        pending_ids.push_back(loot_id);
    }
    for (const uint64_t loot_id : pending_ids) {
        const auto claim_it = claims_.find(loot_id);
        if (claim_it == claims_.end()) continue;
        const GameGroundLootPickupClaim claim = claim_it->second;
        auto receipt = player_has_durable_receipt(claim);
        if (!receipt) return receipt.error();
        auto resolved = *receipt ? remove_world_record(claim) : restore_world_record(claim);
        if (!resolved) {
            auto error = resolved.error();
            error.with_context("GameServerGroundLootPickupPersistence::recover(resolve world)");
            return error;
        }
        SNT_LOG_WARN("Recovered interrupted ground loot pickup %llu for player '%s' as %s-owned",
                     static_cast<unsigned long long>(claim.loot_id), claim.account_id.c_str(),
                     *receipt ? "player" : "world");
        if (auto result = erase_claim(claim.loot_id); !result) {
            auto error = result.error();
            error.with_context("GameServerGroundLootPickupPersistence::recover(clear journal)");
            return error;
        }
        if (*receipt && player_lifecycle_ != nullptr) {
            player_lifecycle_->acknowledge_ground_loot_claim(claim.account_id, claim.loot_id);
        }
    }
    return {};
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::begin_pickup(
    GameGroundLootPickupClaim claim) {
    if (!recovered_) {
        return invalid_state("Ground loot pickup persistence was not recovered before use");
    }
    if (auto result = validate_claim(claim); !result) return result.error();
    if (claims_.contains(claim.loot_id)) {
        return invalid_state("Ground loot pickup journal already owns this loot id");
    }
    const uint64_t loot_id = claim.loot_id;
    claims_.emplace(loot_id, std::move(claim));
    if (auto result = persist_claims(); !result) {
        claims_.erase(loot_id);
        auto error = result.error();
        error.with_context("GameServerGroundLootPickupPersistence::begin_pickup");
        return error;
    }
    return {};
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::abandon_pickup(
    uint64_t loot_id) {
    return erase_claim(loot_id);
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::checkpoint_player_claim(
    const GameAuthenticatedPeer& peer, uint64_t loot_id) {
    if (!claims_.contains(loot_id)) {
        return invalid_state("Ground loot pickup persistence has no matching journal claim");
    }
    if (player_lifecycle_ == nullptr) {
        return invalid_state("Ground loot pickup persistence has no player lifecycle");
    }
    return player_lifecycle_->checkpoint_ground_loot_claim(peer, loot_id);
}

snt::core::Expected<void> GameServerGroundLootPickupPersistence::finalize_pickup(uint64_t loot_id) {
    const auto claim = claims_.find(loot_id);
    if (claim == claims_.end()) {
        return invalid_state("Ground loot pickup persistence has no matching journal claim");
    }
    if (auto result = persist_chunk(claim->second.chunk); !result) return result.error();
    const std::string account_id = claim->second.account_id;
    if (auto result = erase_claim(loot_id); !result) return result.error();
    if (player_lifecycle_ != nullptr) {
        player_lifecycle_->acknowledge_ground_loot_claim(account_id, loot_id);
    }
    return {};
}

}  // namespace snt::game::replication
