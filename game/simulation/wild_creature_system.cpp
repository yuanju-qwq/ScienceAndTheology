// Near-field wild creature projection and interaction implementation.

#define SNT_LOG_CHANNEL "game.wild_creature"
#include "core/log.h"

#include "game/simulation/wild_creature_system.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace snt::game {
namespace {

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr float kCaptiveTargetReachedDistance = 0.075f;
constexpr uint64_t kPlayerDamageErrorLogIntervalTicks = 100;

void hash_byte(uint64_t& hash, uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_uint32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t byte_index = 0; byte_index < 4; ++byte_index) {
        hash_byte(hash, static_cast<uint8_t>(value >> (byte_index * 8)));
    }
}

void hash_uint64(uint64_t& hash, uint64_t value) noexcept {
    for (uint32_t byte_index = 0; byte_index < 8; ++byte_index) {
        hash_byte(hash, static_cast<uint8_t>(value >> (byte_index * 8)));
    }
}

void hash_string(uint64_t& hash, const std::string& value) noexcept {
    for (const unsigned char character : value) hash_byte(hash, character);
    hash_byte(hash, 0);
}

[[nodiscard]] bool valid_positive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] uint64_t saturation_add(uint64_t left, uint64_t right) noexcept {
    return right > std::numeric_limits<uint64_t>::max() - left
        ? std::numeric_limits<uint64_t>::max()
        : left + right;
}

[[nodiscard]] uint64_t splitmix64(uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

[[nodiscard]] int64_t saturating_tick(uint64_t tick) noexcept {
    constexpr uint64_t kMaximumTick = static_cast<uint64_t>(
        std::numeric_limits<int64_t>::max());
    return static_cast<int64_t>(std::min(tick, kMaximumTick));
}

[[nodiscard]] int64_t saturating_tick_add(int64_t tick, uint64_t duration) noexcept {
    constexpr uint64_t kMaximumTick = static_cast<uint64_t>(
        std::numeric_limits<int64_t>::max());
    const uint64_t base = tick <= 0 ? 0u : static_cast<uint64_t>(tick);
    return static_cast<int64_t>(std::min(saturation_add(base, duration), kMaximumTick));
}

[[nodiscard]] int32_t floor_divide(int32_t value, int32_t divisor) noexcept {
    const int32_t quotient = value / divisor;
    const int32_t remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

[[nodiscard]] int32_t local_coordinate(int32_t value, int32_t divisor) noexcept {
    return value - floor_divide(value, divisor) * divisor;
}

[[nodiscard]] int32_t bounded_floor(float value, int32_t fallback) noexcept {
    if (!std::isfinite(value) ||
        value < static_cast<float>(std::numeric_limits<int32_t>::min()) ||
        value > static_cast<float>(std::numeric_limits<int32_t>::max())) {
        return fallback;
    }
    return static_cast<int32_t>(std::floor(value));
}

[[nodiscard]] float clamp_to_pen(float value, int32_t minimum, int32_t maximum) noexcept {
    const float lower = static_cast<float>(minimum);
    const float upper = static_cast<float>(maximum) + 0.999f;
    return std::clamp(std::isfinite(value) ? value : lower + 0.5f, lower, upper);
}

[[nodiscard]] int32_t clamp_to_range(int64_t value, int32_t minimum, int32_t maximum) noexcept {
    return static_cast<int32_t>(std::clamp<int64_t>(value, minimum, maximum));
}

[[nodiscard]] uint64_t captive_wander_seed(const ChunkKey& chunk,
                                            const CaptiveCreature& creature,
                                            uint64_t source_tick) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    hash_string(hash, chunk.dimension_id);
    hash_uint32(hash, static_cast<uint32_t>(chunk.chunk_x));
    hash_uint32(hash, static_cast<uint32_t>(chunk.chunk_y));
    hash_uint32(hash, static_cast<uint32_t>(chunk.chunk_z));
    hash_uint64(hash, creature.runtime_id);
    hash_uint64(hash, source_tick);
    return splitmix64(hash);
}

[[nodiscard]] uint64_t wild_wander_seed(
    const GameCreaturePresentationState& creature, uint64_t source_tick) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    hash_string(hash, creature.chunk.dimension_id);
    hash_uint32(hash, static_cast<uint32_t>(creature.chunk.chunk_x));
    hash_uint32(hash, static_cast<uint32_t>(creature.chunk.chunk_y));
    hash_uint32(hash, static_cast<uint32_t>(creature.chunk.chunk_z));
    hash_uint64(hash, creature.entity_id);
    hash_uint64(hash, source_tick);
    return splitmix64(hash);
}

}  // namespace

GameWildCreatureSystem::GameWildCreatureSystem(
    GameEcosystemSystem& ecosystem, ChunkRegistry& chunks,
    GameChunkSidecarRegistry& sidecars, const CreatureSpeciesRegistry& species_catalog,
    GameWildCreatureConfig config) noexcept
    : ecosystem_(&ecosystem), chunks_(&chunks), sidecars_(&sidecars),
      species_catalog_(&species_catalog), config_(std::move(config)) {}

void GameWildCreatureSystem::request_wild_proxy_rebalance(
    const GameEcosystemWildProxyRebalanceRequest& request) {
    std::set<uint64_t> desired_ids;
    for (const GameEcosystemWildProxyPlan& plan : request.proxies) {
        if (plan.stable_id == 0 || species_catalog_ == nullptr ||
            species_catalog_->get_species(plan.species_id) == nullptr) {
            continue;
        }
        const auto suppressed = suppressed_proxy_until_tick_.find(plan.stable_id);
        if (suppressed != suppressed_proxy_until_tick_.end() &&
            request.source_tick < suppressed->second) {
            continue;
        }
        if (suppressed != suppressed_proxy_until_tick_.end()) {
            suppressed_proxy_until_tick_.erase(suppressed);
        }
        desired_ids.insert(plan.stable_id);

        // Promotion reuses the deterministic proxy identity. The client
        // receives one upsert for the interactive state instead of a far
        // visual despawn/spawn pair that would make a boundary crossing pop.
        far_visual_creatures_.erase(plan.stable_id);

        const auto existing = wild_creatures_.find(plan.stable_id);
        if (existing != wild_creatures_.end()) {
            // Stable proxy identity preserves health/position between low-rate
            // aggregate rebalances. Changed content is reconciled as a new
            // temporary creature rather than mutating its species in place.
            if (existing->second.state.species_id == plan.species_id &&
                existing->second.state.role == plan.role &&
                existing->second.state.chunk == request.chunk) {
                continue;
            }
            emit(GameCreaturePresentationEventKind::kDespawned, request.source_tick,
                 existing->second.state);
            wild_creatures_.erase(existing);
        }

        WildCreature creature{
            .state = make_wild_state(request, plan, true),
            .spawn_tick = request.source_tick,
        };
        creature.wander_target_x = creature.state.position_x;
        creature.wander_target_y = creature.state.position_y;
        creature.wander_target_z = creature.state.position_z;
        const GameCreaturePresentationState state = creature.state;
        wild_creatures_.insert_or_assign(plan.stable_id, std::move(creature));
        emit(GameCreaturePresentationEventKind::kSpawned, request.source_tick, state);
    }

    std::vector<uint64_t> removals;
    for (const auto& [id, creature] : wild_creatures_) {
        if (creature.state.chunk == request.chunk && !desired_ids.contains(id)) {
            removals.push_back(id);
        }
    }
    for (const uint64_t id : removals) {
        const auto existing = wild_creatures_.find(id);
        if (existing == wild_creatures_.end()) continue;
        emit(GameCreaturePresentationEventKind::kDespawned, request.source_tick,
             existing->second.state);
        wild_creatures_.erase(existing);
    }
}

void GameWildCreatureSystem::tick_interactive_wild_creatures(
    const GameEcosystemWildTickRequest& request) {
    // All near-field decisions use this start-of-tick snapshot. Wild proxies
    // remain a visual/interactable projection of aggregate population, so
    // pursuit here never kills an individual or changes population density.
    std::vector<GameCreaturePresentationState> nearby_creatures;
    nearby_creatures.reserve(wild_creatures_.size());
    for (const auto& [id, creature] : wild_creatures_) {
        static_cast<void>(id);
        if (creature.state.is_interactive && creature.state.chunk == request.chunk) {
            nearby_creatures.push_back(creature.state);
        }
    }

    // The ecosystem invokes this once per active chunk. Cache the transport-
    // free player snapshot for the current source tick so a large interest
    // ring does not repeatedly query server-owned player state.
    if (!player_target_cache_tick_.has_value() ||
        *player_target_cache_tick_ != request.source_tick) {
        player_target_cache_.clear();
        if (player_target_provider_ != nullptr) {
            player_target_cache_ = player_target_provider_->active_wild_creature_player_targets();
        }
        player_target_cache_tick_ = request.source_tick;
    }

    for (auto& [id, creature] : wild_creatures_) {
        static_cast<void>(id);
        if (!creature.state.is_interactive || creature.state.chunk != request.chunk) continue;
        if (advance_wild_creature(creature, nearby_creatures, player_target_cache_,
                                  request.source_tick) &&
            should_emit_wild_update(request.source_tick)) {
            emit(GameCreaturePresentationEventKind::kUpdated, request.source_tick,
                 creature.state);
        }
    }
}

bool GameWildCreatureSystem::advance_wild_creature(
    WildCreature& creature,
    const std::vector<GameCreaturePresentationState>& nearby_creatures,
    const std::vector<GameWildCreaturePlayerTarget>& nearby_players,
    uint64_t source_tick) {
    if (!creature.state.is_interactive || creature.spawn_tick == source_tick) return false;
    const CreatureSpeciesDef* const species = species_catalog_ == nullptr
        ? nullptr
        : species_catalog_->get_species(creature.state.species_id);
    if (species == nullptr) return false;

    if (creature.state.role == CreatureRole::HERBIVORE) {
        const float detection_radius = valid_positive(species->flee_detection_radius)
            ? species->flee_detection_radius
            : config_.wild_fallback_flee_detection_radius;
        if (valid_positive(detection_radius)) {
            const GameCreaturePresentationState* closest_predator = nullptr;
            float closest_distance_squared = detection_radius * detection_radius;
            for (const GameCreaturePresentationState& predator : nearby_creatures) {
                if (predator.entity_id == creature.state.entity_id ||
                    predator.role != CreatureRole::PREDATOR) {
                    continue;
                }
                const float delta_x = creature.state.position_x - predator.position_x;
                const float delta_z = creature.state.position_z - predator.position_z;
                const float distance_squared = delta_x * delta_x + delta_z * delta_z;
                if (std::isfinite(distance_squared) && distance_squared <= closest_distance_squared) {
                    closest_predator = &predator;
                    closest_distance_squared = distance_squared;
                }
            }
            if (closest_predator != nullptr) {
                float direction_x = creature.state.position_x - closest_predator->position_x;
                float direction_z = creature.state.position_z - closest_predator->position_z;
                const float direction_length = std::sqrt(
                    direction_x * direction_x + direction_z * direction_z);
                if (direction_length <= kCaptiveTargetReachedDistance) {
                    const uint64_t seed = wild_wander_seed(creature.state, source_tick);
                    constexpr float kTwoPi = 6.28318530717958647692f;
                    const float angle = static_cast<float>(seed & 0xffffu) / 65536.0f * kTwoPi;
                    direction_x = std::cos(angle);
                    direction_z = std::sin(angle);
                } else {
                    direction_x /= direction_length;
                    direction_z /= direction_length;
                }
                const float flee_distance = std::max(1.0f, std::min(detection_radius, 3.0f));
                constexpr std::array<std::array<float, 2>, 4> kFleeDirections{{
                    {{1.0f, 0.0f}}, {{0.0f, 1.0f}}, {{0.0f, -1.0f}}, {{-1.0f, 0.0f}},
                }};
                for (const auto& rotation : kFleeDirections) {
                    const float candidate_direction_x = direction_x * rotation[0] -
                        direction_z * rotation[1];
                    const float candidate_direction_z = direction_x * rotation[1] +
                        direction_z * rotation[0];
                    if (move_wild_toward(
                            creature,
                            creature.state.position_x + candidate_direction_x * flee_distance,
                            creature.state.position_y,
                            creature.state.position_z + candidate_direction_z * flee_distance)) {
                        return true;
                    }
                }
                return false;
            }
        }
    }

    if (creature.state.role == CreatureRole::PREDATOR) {
        float closest_player_distance_squared = std::numeric_limits<float>::infinity();
        const GameWildCreaturePlayerTarget* const closest_player =
            closest_player_target(creature, nearby_players, species->player_detection_radius,
                                  closest_player_distance_squared);
        const GameCreaturePresentationState* closest_herbivore = nullptr;
        float closest_distance_squared = std::numeric_limits<float>::infinity();
        for (const GameCreaturePresentationState& herbivore : nearby_creatures) {
            if (herbivore.entity_id == creature.state.entity_id ||
                herbivore.role != CreatureRole::HERBIVORE) {
                continue;
            }
            const float delta_x = herbivore.position_x - creature.state.position_x;
            const float delta_z = herbivore.position_z - creature.state.position_z;
            const float distance_squared = delta_x * delta_x + delta_z * delta_z;
            if (!std::isfinite(distance_squared)) continue;
            if (closest_herbivore == nullptr || distance_squared < closest_distance_squared ||
                (distance_squared == closest_distance_squared &&
                 herbivore.entity_id < closest_herbivore->entity_id)) {
                closest_herbivore = &herbivore;
                closest_distance_squared = distance_squared;
            }
        }
        const bool target_player = closest_player != nullptr &&
            (closest_herbivore == nullptr ||
             closest_player_distance_squared <= closest_distance_squared);
        if (target_player) {
            const float attack_range = species->player_attack_range;
            const float attack_damage = species->player_attack_damage;
            const uint64_t attack_cooldown = species->player_attack_cooldown_ticks;
            if (valid_positive(attack_range) && valid_positive(attack_damage) &&
                attack_cooldown != 0 &&
                closest_player_distance_squared <= attack_range * attack_range) {
                try_attack_player(creature, *closest_player, attack_damage,
                                  attack_cooldown, source_tick);
                return false;
            }
            // A target outside attack reach remains a pursuit target. Do not
            // fall through to prey/wander, otherwise a predator oscillates at
            // the player detection boundary.
            return move_wild_toward(creature, closest_player->feet_x,
                                    closest_player->feet_y,
                                    closest_player->feet_z);
        }
        if (closest_herbivore != nullptr) {
            // A blocked pursuit remains stationary until the next snapshot;
            // falling through to wander would move a predator away from prey.
            return move_wild_toward(creature, closest_herbivore->position_x,
                                    closest_herbivore->position_y,
                                    closest_herbivore->position_z);
        }
    }

    if (config_.wild_wander_interval_ticks == 0) return false;
    const bool target_is_finite = std::isfinite(creature.wander_target_x) &&
        std::isfinite(creature.wander_target_y) && std::isfinite(creature.wander_target_z);
    if (!target_is_finite || creature.next_wander_tick <= source_tick) {
        if (!choose_wild_wander_target(creature, source_tick)) return false;
    }
    return move_wild_toward(creature, creature.wander_target_x, creature.wander_target_y,
                             creature.wander_target_z);
}

const GameWildCreaturePlayerTarget* GameWildCreatureSystem::closest_player_target(
    const WildCreature& creature,
    const std::vector<GameWildCreaturePlayerTarget>& nearby_players,
    float detection_radius, float& out_distance_squared) const noexcept {
    out_distance_squared = std::numeric_limits<float>::infinity();
    if (!valid_positive(detection_radius)) return nullptr;
    const GameWildCreaturePlayerTarget* closest = nullptr;
    const float maximum_distance_squared = detection_radius * detection_radius;
    for (const GameWildCreaturePlayerTarget& player : nearby_players) {
        if (player.account_id.empty() || player.dimension_id != creature.state.chunk.dimension_id ||
            !std::isfinite(player.feet_x) || !std::isfinite(player.feet_y) ||
            !std::isfinite(player.feet_z)) {
            continue;
        }
        const float delta_x = player.feet_x - creature.state.position_x;
        const float delta_z = player.feet_z - creature.state.position_z;
        const float distance_squared = delta_x * delta_x + delta_z * delta_z;
        if (!std::isfinite(distance_squared) || distance_squared > maximum_distance_squared) {
            continue;
        }
        if (closest == nullptr || distance_squared < out_distance_squared ||
            (distance_squared == out_distance_squared && player.account_id < closest->account_id)) {
            closest = &player;
            out_distance_squared = distance_squared;
        }
    }
    return closest;
}

void GameWildCreatureSystem::try_attack_player(
    WildCreature& creature, const GameWildCreaturePlayerTarget& target,
    float attack_damage, uint64_t attack_cooldown_ticks, uint64_t source_tick) {
    if (player_damage_sink_ == nullptr || source_tick < creature.next_player_attack_tick) return;
    auto result = player_damage_sink_->apply_wild_creature_player_damage({
        .wild_entity_id = creature.state.entity_id,
        .species_id = creature.state.species_id,
        .target_account_id = target.account_id,
        .damage = attack_damage,
        .source_tick = source_tick,
    });
    if (!result) {
        const auto previous = player_damage_error_ticks_.find(target.account_id);
        if (previous == player_damage_error_ticks_.end() ||
            source_tick >= previous->second + kPlayerDamageErrorLogIntervalTicks) {
            SNT_LOG_WARN("Wild creature %llu could not damage player '%s': %s",
                         static_cast<unsigned long long>(creature.state.entity_id),
                         target.account_id.c_str(), result.error().format().c_str());
            player_damage_error_ticks_.insert_or_assign(target.account_id, source_tick);
        }
        return;
    }
    player_damage_error_ticks_.erase(target.account_id);
    creature.next_player_attack_tick = saturation_add(source_tick, attack_cooldown_ticks);
}

bool GameWildCreatureSystem::choose_wild_wander_target(
    WildCreature& creature, uint64_t source_tick) const {
    if (chunks_ == nullptr || config_.wild_wander_interval_ticks == 0) return false;
    const ChunkKey& chunk = creature.state.chunk;
    const VoxelChunk* const voxel_chunk = chunks_->get_chunk(
        chunk.dimension_id, chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);
    if (voxel_chunk == nullptr || voxel_chunk->terrain.size_x <= 0 ||
        voxel_chunk->terrain.size_y <= 1 || voxel_chunk->terrain.size_z <= 0) {
        return false;
    }

    constexpr int32_t kChunkSize = VoxelChunk::kChunkSize;
    const int64_t base_x = static_cast<int64_t>(chunk.chunk_x) * kChunkSize;
    const int64_t base_y = static_cast<int64_t>(chunk.chunk_y) * kChunkSize;
    const int64_t base_z = static_cast<int64_t>(chunk.chunk_z) * kChunkSize;
    const int64_t current_world_x = bounded_floor(creature.state.position_x, 0);
    const int64_t current_world_z = bounded_floor(creature.state.position_z, 0);
    const int32_t current_local_x = static_cast<int32_t>(std::clamp<int64_t>(
        current_world_x - base_x, 0, voxel_chunk->terrain.size_x - 1));
    const int32_t current_local_z = static_cast<int32_t>(std::clamp<int64_t>(
        current_world_z - base_z, 0, voxel_chunk->terrain.size_z - 1));
    const CreatureSpeciesDef* const species = species_catalog_ == nullptr
        ? nullptr
        : species_catalog_->get_species(creature.state.species_id);
    const int64_t chunk_extent = std::max<int64_t>(
        voxel_chunk->terrain.size_x - 1, voxel_chunk->terrain.size_z - 1);
    const float configured_radius = species != nullptr && valid_positive(species->wander_radius)
        ? species->wander_radius
        : static_cast<float>(std::min<int64_t>(chunk_extent, 8));
    const int64_t radius = std::clamp<int64_t>(
        static_cast<int64_t>(std::floor(std::max(1.0f, configured_radius))),
        1, std::max<int64_t>(1, chunk_extent));
    const uint64_t span = static_cast<uint64_t>(radius * 2 + 1);
    const uint32_t attempts = std::max<uint32_t>(1, config_.wild_wander_target_attempts);
    uint64_t seed = wild_wander_seed(creature.state, source_tick);
    for (uint32_t attempt = 0; attempt < attempts; ++attempt) {
        seed = splitmix64(seed);
        const int64_t offset_x = static_cast<int64_t>(seed % span) - radius;
        seed = splitmix64(seed);
        const int64_t offset_z = static_cast<int64_t>(seed % span) - radius;
        const int32_t local_x = static_cast<int32_t>(std::clamp<int64_t>(
            static_cast<int64_t>(current_local_x) + offset_x,
            0, voxel_chunk->terrain.size_x - 1));
        const int32_t local_z = static_cast<int32_t>(std::clamp<int64_t>(
            static_cast<int64_t>(current_local_z) + offset_z,
            0, voxel_chunk->terrain.size_z - 1));
        for (int local_y = voxel_chunk->terrain.size_y - 1; local_y >= 1; --local_y) {
            const int64_t world_x = base_x + local_x;
            const int64_t world_y = base_y + local_y;
            const int64_t world_z = base_z + local_z;
            if (world_x < std::numeric_limits<int32_t>::min() ||
                world_x > std::numeric_limits<int32_t>::max() ||
                world_y < std::numeric_limits<int32_t>::min() ||
                world_y > std::numeric_limits<int32_t>::max() ||
                world_z < std::numeric_limits<int32_t>::min() ||
                world_z > std::numeric_limits<int32_t>::max() ||
                !is_wild_cell_walkable(chunk, static_cast<int32_t>(world_x),
                                       static_cast<int32_t>(world_y),
                                       static_cast<int32_t>(world_z))) {
                continue;
            }
            creature.wander_target_x = static_cast<float>(world_x) + 0.5f;
            creature.wander_target_y = static_cast<float>(world_y);
            creature.wander_target_z = static_cast<float>(world_z) + 0.5f;
            creature.next_wander_tick = saturation_add(
                source_tick, config_.wild_wander_interval_ticks);
            return true;
        }
    }
    creature.next_wander_tick = saturation_add(source_tick, config_.wild_wander_interval_ticks);
    return false;
}

bool GameWildCreatureSystem::move_wild_toward(
    WildCreature& creature, float target_x, float target_y, float target_z) const {
    if (!std::isfinite(target_x) || !std::isfinite(target_y) || !std::isfinite(target_z) ||
        !std::isfinite(creature.state.position_x) ||
        !std::isfinite(creature.state.position_y) || !std::isfinite(creature.state.position_z)) {
        return false;
    }
    const CreatureSpeciesDef* const species = species_catalog_ == nullptr
        ? nullptr
        : species_catalog_->get_species(creature.state.species_id);
    const float move_speed = species != nullptr && valid_positive(species->move_speed)
        ? species->move_speed
        : config_.wild_fallback_move_speed;
    if (!valid_positive(move_speed)) return false;

    const float delta_x = target_x - creature.state.position_x;
    const float delta_y = target_y - creature.state.position_y;
    const float delta_z = target_z - creature.state.position_z;
    const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
    if (!std::isfinite(distance) || distance <= kCaptiveTargetReachedDistance) return false;
    const float step = std::min(move_speed, distance);
    const float candidate_x = creature.state.position_x + delta_x / distance * step;
    const float candidate_y = creature.state.position_y + delta_y / distance * step;
    const float candidate_z = creature.state.position_z + delta_z / distance * step;
    const int32_t block_x = bounded_floor(candidate_x, 0);
    const int32_t block_y = bounded_floor(candidate_y, 0);
    const int32_t block_z = bounded_floor(candidate_z, 0);
    if (!is_wild_cell_walkable(creature.state.chunk, block_x, block_y, block_z)) return false;
    creature.state.position_x = candidate_x;
    creature.state.position_y = candidate_y;
    creature.state.position_z = candidate_z;
    return true;
}

bool GameWildCreatureSystem::is_wild_cell_walkable(
    const ChunkKey& chunk, int32_t block_x, int32_t block_y, int32_t block_z) const {
    if (chunks_ == nullptr || chunk.dimension_id.empty() ||
        block_y == std::numeric_limits<int32_t>::min()) {
        return false;
    }
    constexpr int32_t kChunkSize = VoxelChunk::kChunkSize;
    const VoxelChunk* const voxel_chunk = chunks_->get_chunk(
        chunk.dimension_id, chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);
    if (voxel_chunk == nullptr) return false;
    const int64_t base_x = static_cast<int64_t>(chunk.chunk_x) * kChunkSize;
    const int64_t base_y = static_cast<int64_t>(chunk.chunk_y) * kChunkSize;
    const int64_t base_z = static_cast<int64_t>(chunk.chunk_z) * kChunkSize;
    const int64_t local_x = static_cast<int64_t>(block_x) - base_x;
    const int64_t local_y = static_cast<int64_t>(block_y) - base_y;
    const int64_t local_z = static_cast<int64_t>(block_z) - base_z;
    if (local_x < 0 || local_y <= 0 || local_z < 0 ||
        local_x >= voxel_chunk->terrain.size_x || local_y >= voxel_chunk->terrain.size_y ||
        local_z >= voxel_chunk->terrain.size_z) {
        return false;
    }
    const TerrainCell& space = voxel_chunk->terrain.cell_at(
        static_cast<int>(local_x), static_cast<int>(local_y), static_cast<int>(local_z));
    const TerrainCell& floor = voxel_chunk->terrain.cell_at(
        static_cast<int>(local_x), static_cast<int>(local_y - 1), static_cast<int>(local_z));
    return !space.is_solid() && !space.has_fluid() && floor.is_solid();
}

bool GameWildCreatureSystem::should_emit_wild_update(uint64_t source_tick) const noexcept {
    return config_.wild_presentation_interval_ticks == 0 ||
        source_tick % config_.wild_presentation_interval_ticks == 0;
}

void GameWildCreatureSystem::request_far_visual_rebalance(
    const GameEcosystemFarVisualRebalanceRequest& request) {
    std::set<uint64_t> desired_ids;
    for (const GameEcosystemWildProxyPlan& plan : request.proxies) {
        if (plan.stable_id == 0 || species_catalog_ == nullptr ||
            species_catalog_->get_species(plan.species_id) == nullptr) {
            continue;
        }
        const auto suppressed = suppressed_proxy_until_tick_.find(plan.stable_id);
        if (suppressed != suppressed_proxy_until_tick_.end() &&
            request.source_tick < suppressed->second) {
            continue;
        }
        if (suppressed != suppressed_proxy_until_tick_.end()) {
            suppressed_proxy_until_tick_.erase(suppressed);
        }
        desired_ids.insert(plan.stable_id);

        // Interactive representatives are more precise than far visuals and
        // own the same stable id while the player remains close enough.
        if (wild_creatures_.contains(plan.stable_id)) {
            far_visual_creatures_.erase(plan.stable_id);
            continue;
        }

        const auto existing = far_visual_creatures_.find(plan.stable_id);
        if (existing != far_visual_creatures_.end()) {
            if (existing->second.state.species_id == plan.species_id &&
                existing->second.state.role == plan.role &&
                existing->second.state.chunk == request.chunk) {
                continue;
            }
            emit(GameCreaturePresentationEventKind::kDespawned, request.source_tick,
                 existing->second.state);
            far_visual_creatures_.erase(existing);
        }

        WildCreature creature{
            .state = make_wild_state(request, plan, false),
            .spawn_tick = request.source_tick,
        };
        creature.wander_target_x = creature.state.position_x;
        creature.wander_target_y = creature.state.position_y;
        creature.wander_target_z = creature.state.position_z;
        const GameCreaturePresentationState state = creature.state;
        far_visual_creatures_.insert_or_assign(plan.stable_id, std::move(creature));
        emit(GameCreaturePresentationEventKind::kSpawned, request.source_tick, state);
    }

    std::vector<uint64_t> removals;
    for (const auto& [id, creature] : far_visual_creatures_) {
        if (creature.state.chunk == request.chunk && !desired_ids.contains(id)) {
            removals.push_back(id);
        }
    }
    for (const uint64_t id : removals) {
        const auto existing = far_visual_creatures_.find(id);
        if (existing == far_visual_creatures_.end()) continue;
        emit(GameCreaturePresentationEventKind::kDespawned, request.source_tick,
             existing->second.state);
        far_visual_creatures_.erase(existing);
    }
}

void GameWildCreatureSystem::tick_captive_creatures(
    const GameEcosystemCaptiveTickRequest& request) {
    if (sidecars_ == nullptr) return;
    GameChunkSidecar* const sidecar = sidecars_->get(request.chunk);
    if (sidecar == nullptr || !sidecar->has_captive_creatures) return;

    struct PendingBirth {
        CaptiveCreature creature;
        uint64_t mother_id = 0;
    };

    const int64_t current_tick = saturating_tick(request.source_tick);
    const bool publish_continuous_updates = should_emit_captive_update(request.source_tick);
    std::set<uint64_t> current_ids;
    std::vector<PendingBirth> pending_births;
    pending_births.reserve(sidecar->captive_creatures.size());
    for (size_t slot = 0; slot < sidecar->captive_creatures.size(); ++slot) {
        CaptiveCreature& captive = sidecar->captive_creatures[slot];
        bool changed = false;
        bool matured = false;
        if (captive.runtime_id == 0) {
            captive.runtime_id = stable_captive_runtime_id(request.chunk, captive.species_id, slot);
            changed = true;
        }

        if (captive.age_stage == CreatureAgeStage::BABY && captive.grow_up_tick > 0 &&
            current_tick >= captive.grow_up_tick) {
            captive.age_stage = CreatureAgeStage::ADULT;
            captive.grow_up_tick = 0;
            changed = true;
            matured = true;
        }

        if (captive.is_pregnant && captive.gestation_end_tick > 0 &&
            current_tick >= captive.gestation_end_tick) {
            const bool has_capacity = config_.max_captive_creatures_per_chunk != 0 &&
                sidecar->captive_creatures.size() + pending_births.size() <
                    config_.max_captive_creatures_per_chunk;
            if (!has_capacity) {
                SNT_LOG_WARN("Cancelling captive birth because chunk '%s' reached its creature limit",
                             request.chunk.dimension_id.c_str());
                captive.is_pregnant = false;
                captive.gestation_end_tick = 0;
                captive.partner_species_id = 0;
                changed = true;
            } else {
                const CreatureSpeciesDef* const species = species_catalog_ == nullptr
                    ? nullptr
                    : species_catalog_->get_species(captive.species_id);
                const float child_health = species != nullptr && valid_positive(species->base_health)
                    ? species->base_health
                    : std::max(0.01f, captive.health);
                const float child_x = clamp_to_pen(captive.pos_x, captive.bounds_min_x,
                                                   captive.bounds_max_x);
                const float child_y = clamp_to_pen(captive.pos_y, captive.bounds_min_y,
                                                   captive.bounds_max_y);
                const float child_z = clamp_to_pen(captive.pos_z, captive.bounds_min_z,
                                                   captive.bounds_max_z);
                pending_births.push_back({
                    .creature = {
                        .species_id = captive.species_id,
                        .role = species != nullptr ? species->role : captive.role,
                        .age_stage = CreatureAgeStage::BABY,
                        .pos_x = child_x,
                        .pos_y = child_y,
                        .pos_z = child_z,
                        .wander_target_x = child_x,
                        .wander_target_y = child_y,
                        .wander_target_z = child_z,
                        .next_wander_tick = current_tick,
                        .bounds_min_x = captive.bounds_min_x,
                        .bounds_min_y = captive.bounds_min_y,
                        .bounds_min_z = captive.bounds_min_z,
                        .bounds_max_x = captive.bounds_max_x,
                        .bounds_max_y = captive.bounds_max_y,
                        .bounds_max_z = captive.bounds_max_z,
                        .health = child_health,
                        .tame_progress = 1.0f,
                        .is_tamed = true,
                        .birth_tick = current_tick,
                        .grow_up_tick = saturating_tick_add(
                            current_tick, std::max<uint64_t>(1, config_.captive_growth_ticks)),
                    },
                    .mother_id = captive.runtime_id,
                });
                captive.is_pregnant = false;
                captive.gestation_end_tick = 0;
                captive.partner_species_id = 0;
                changed = true;
            }
        }

        if (advance_captive_wander(request.chunk, captive, request.source_tick)) {
            changed = true;
        }
        const GameCreaturePresentationState state = make_captive_state(request.chunk, captive);
        current_ids.insert(captive.runtime_id);
        const auto [known, inserted] = known_captive_creatures_.insert_or_assign(
            captive.runtime_id, state);
        static_cast<void>(known);
        if (inserted) {
            emit(GameCreaturePresentationEventKind::kSpawned, request.source_tick, state);
        } else if (matured) {
            emit(GameCreaturePresentationEventKind::kMatured, request.source_tick, state);
        } else if (changed && publish_continuous_updates) {
            emit(GameCreaturePresentationEventKind::kUpdated, request.source_tick, state);
        }
    }

    for (PendingBirth& pending : pending_births) {
        const size_t slot = sidecar->captive_creatures.size();
        pending.creature.runtime_id = stable_captive_runtime_id(
            request.chunk, pending.creature.species_id, slot);
        sidecar->captive_creatures.push_back(std::move(pending.creature));
        CaptiveCreature& newborn = sidecar->captive_creatures.back();
        const GameCreaturePresentationState state = make_captive_state(request.chunk, newborn);
        current_ids.insert(newborn.runtime_id);
        known_captive_creatures_.insert_or_assign(newborn.runtime_id, state);
        emit(GameCreaturePresentationEventKind::kBorn, request.source_tick, state);
        SNT_LOG_INFO("Captive creature born: chunk='%s' mother=%llu child=%llu species=%u",
                     request.chunk.dimension_id.c_str(),
                     static_cast<unsigned long long>(pending.mother_id),
                     static_cast<unsigned long long>(newborn.runtime_id),
                     static_cast<unsigned>(newborn.species_id));
    }
    for (auto known = known_captive_creatures_.begin();
         known != known_captive_creatures_.end();) {
        if (known->second.chunk == request.chunk && !current_ids.contains(known->first)) {
            emit(GameCreaturePresentationEventKind::kDespawned, request.source_tick,
                 known->second);
            known = known_captive_creatures_.erase(known);
        } else {
            ++known;
        }
    }
}

std::optional<GameCreaturePresentationState> GameWildCreatureSystem::find_wild_creature(
    uint64_t wild_entity_id) const {
    const auto found = wild_creatures_.find(wild_entity_id);
    if (found == wild_creatures_.end()) return std::nullopt;
    return found->second.state;
}

std::optional<GameCreaturePresentationState> GameWildCreatureSystem::find_captive_creature(
    uint64_t captive_entity_id) const {
    if (captive_entity_id == 0 || sidecars_ == nullptr) return std::nullopt;
    const auto known = known_captive_creatures_.find(captive_entity_id);
    if (known != known_captive_creatures_.end()) return known->second;

    std::optional<GameCreaturePresentationState> result;
    sidecars_->for_each([&](const ChunkKey& chunk, const GameChunkSidecar& sidecar) {
        if (result.has_value() || !sidecar.has_captive_creatures) return;
        const auto found = std::find_if(
            sidecar.captive_creatures.begin(), sidecar.captive_creatures.end(),
            [captive_entity_id](const CaptiveCreature& creature) {
                return creature.runtime_id == captive_entity_id;
            });
        if (found != sidecar.captive_creatures.end()) {
            result = make_captive_state(chunk, *found);
        }
    });
    return result;
}

GameWildCreatureAttackResult GameWildCreatureSystem::apply_damage(
    uint64_t wild_entity_id, float damage, uint64_t source_tick) {
    GameWildCreatureAttackResult result;
    if (!valid_positive(damage)) return result;
    const auto found = wild_creatures_.find(wild_entity_id);
    if (found == wild_creatures_.end()) return result;

    WildCreature& creature = found->second;
    result.hit = true;
    result.wild_entity_id = creature.state.entity_id;
    result.species_id = creature.state.species_id;
    result.chunk = creature.state.chunk;
    result.damage_dealt = std::min(damage, creature.state.health);
    creature.state.health = std::max(0.0f, creature.state.health - result.damage_dealt);
    result.remaining_health = creature.state.health;
    emit(GameCreaturePresentationEventKind::kDamaged, source_tick, creature.state);

    if (creature.state.health > 0.0f) return result;

    result.killed = true;
    const GameCreaturePresentationState final_state = creature.state;
    if (ecosystem_ != nullptr) {
        if (auto recorded = ecosystem_->record_hunt(
                final_state.chunk, final_state.role, 0.0f, source_tick);
            !recorded) {
            SNT_LOG_WARN("Wild creature kill could not record hunting pressure: %s",
                         recorded.error().format().c_str());
        }
    }
    suppressed_proxy_until_tick_[wild_entity_id] = saturation_add(
        source_tick, config_.killed_proxy_suppression_ticks);
    emit(GameCreaturePresentationEventKind::kKilled, source_tick, final_state);
    emit(GameCreaturePresentationEventKind::kDespawned, source_tick, final_state);
    wild_creatures_.erase(found);
    return result;
}

snt::core::Expected<GameWildCreatureCaptureResult>
GameWildCreatureSystem::capture_wild_creature(
    const GameWildCreatureCaptureRequest& request, uint64_t source_tick) {
    if (request.wild_entity_id == 0 || request.captive_chunk.dimension_id.empty()) {
        return invalid_argument("Wild capture request has an invalid entity or chunk");
    }
    if (!request.pen_bounds.valid()) {
        return invalid_argument("Wild capture request has invalid pen bounds");
    }
    if (!std::isfinite(request.initial_tame_progress) ||
        request.initial_tame_progress < 0.0f || request.initial_tame_progress > 1.0f) {
        return invalid_argument("Wild capture request has invalid tame progress");
    }
    const auto found = wild_creatures_.find(request.wild_entity_id);
    if (found == wild_creatures_.end()) return GameWildCreatureCaptureResult{};
    if (chunks_ == nullptr || chunks_->get_chunk(
            request.captive_chunk.dimension_id, request.captive_chunk.chunk_x,
            request.captive_chunk.chunk_y, request.captive_chunk.chunk_z) == nullptr) {
        return invalid_state("Wild capture target chunk is not loaded");
    }
    const GameCreaturePresentationState wild = found->second.state;
    if (!is_inside_pen(wild, request.pen_bounds)) {
        return invalid_argument("Wild creature is outside the validated pen bounds");
    }

    if (sidecars_ == nullptr || ecosystem_ == nullptr) {
        return invalid_state("Wild creature system is not initialized");
    }
    GameChunkSidecar* sidecar = sidecars_->get(request.captive_chunk);
    if (sidecar == nullptr) {
        sidecars_->set(request.captive_chunk, {});
        sidecar = sidecars_->get(request.captive_chunk);
    }
    if (sidecar == nullptr) return invalid_state("Wild capture sidecar is unavailable");
    if (sidecar->captive_creatures.size() >= config_.max_captive_creatures_per_chunk) {
        return invalid_state("Wild capture pen chunk reached its captive creature limit");
    }
    if (auto recorded = ecosystem_->record_wild_capture(
            wild.chunk, wild.role, 0.0f, source_tick); !recorded) {
        return recorded.error();
    }

    CaptiveCreature captive{
        .runtime_id = wild.entity_id,
        .species_id = wild.species_id,
        .role = wild.role,
        .age_stage = CreatureAgeStage::ADULT,
        .pos_x = wild.position_x,
        .pos_y = wild.position_y,
        .pos_z = wild.position_z,
        .wander_target_x = wild.position_x,
        .wander_target_y = wild.position_y,
        .wander_target_z = wild.position_z,
        .bounds_min_x = request.pen_bounds.min_x,
        .bounds_min_y = request.pen_bounds.min_y,
        .bounds_min_z = request.pen_bounds.min_z,
        .bounds_max_x = request.pen_bounds.max_x,
        .bounds_max_y = request.pen_bounds.max_y,
        .bounds_max_z = request.pen_bounds.max_z,
        .health = wild.health,
        .tame_progress = request.initial_tame_progress,
        .is_tamed = request.initial_tame_progress >= 1.0f,
        .capture_tick = static_cast<int64_t>(source_tick),
        .birth_tick = static_cast<int64_t>(source_tick),
    };
    sidecar->has_captive_creatures = true;
    sidecar->captive_creatures.push_back(captive);
    suppressed_proxy_until_tick_[wild.entity_id] = saturation_add(
        source_tick, config_.captured_proxy_suppression_ticks);
    wild_creatures_.erase(found);
    const GameCreaturePresentationState captive_state = make_captive_state(
        request.captive_chunk, captive);
    known_captive_creatures_.insert_or_assign(captive.runtime_id, captive_state);
    emit(GameCreaturePresentationEventKind::kCaptured, source_tick, captive_state);
    if (captive.is_tamed) {
        emit(GameCreaturePresentationEventKind::kTamed, source_tick, captive_state);
    }
    return GameWildCreatureCaptureResult{
        .captured = true,
        .captive_entity_id = captive.runtime_id,
        .species_id = captive.species_id,
        .captive_chunk = request.captive_chunk,
    };
}

snt::core::Expected<GameCaptiveCreatureFeedResult>
GameWildCreatureSystem::feed_captive_creature(
    uint64_t captive_entity_id, float tame_progress, uint64_t source_tick) {
    if (captive_entity_id == 0 || !valid_positive(tame_progress)) {
        return invalid_argument("Captive feed request has invalid input");
    }
    if (sidecars_ == nullptr) return invalid_state("Wild creature system is not initialized");

    GameCaptiveCreatureFeedResult result;
    ChunkKey owner_chunk;
    GameChunkSidecar* owner_sidecar = nullptr;
    CaptiveCreature* found = nullptr;
    sidecars_->for_each([&](const ChunkKey& chunk, GameChunkSidecar& sidecar) {
        if (found != nullptr || !sidecar.has_captive_creatures) return;
        for (CaptiveCreature& creature : sidecar.captive_creatures) {
            if (creature.runtime_id == captive_entity_id) {
                owner_chunk = chunk;
                owner_sidecar = &sidecar;
                found = &creature;
                return;
            }
        }
    });
    if (found == nullptr) return result;

    result.fed = true;
    result.captive_entity_id = found->runtime_id;
    result.species_id = found->species_id;
    result.chunk = owner_chunk;
    if (!found->is_tamed) {
        found->tame_progress = std::clamp(found->tame_progress + tame_progress, 0.0f, 1.0f);
        if (found->tame_progress >= 1.0f) {
            found->is_tamed = true;
            result.became_tamed = true;
        }
    } else if (owner_sidecar != nullptr &&
               is_breedable(*found, saturating_tick(source_tick))) {
        const size_t reserved_births = static_cast<size_t>(std::count_if(
            owner_sidecar->captive_creatures.begin(), owner_sidecar->captive_creatures.end(),
            [](const CaptiveCreature& creature) { return creature.is_pregnant; }));
        const bool has_capacity = config_.max_captive_creatures_per_chunk != 0 &&
            owner_sidecar->captive_creatures.size() + reserved_births <
                config_.max_captive_creatures_per_chunk;
        if (has_capacity) {
            const auto partner = std::find_if(
                owner_sidecar->captive_creatures.begin(), owner_sidecar->captive_creatures.end(),
                [found, source_tick](const CaptiveCreature& candidate) {
                    return &candidate != found && candidate.runtime_id != 0 &&
                        candidate.species_id == found->species_id && same_pen(*found, candidate) &&
                        is_breedable(candidate, saturating_tick(source_tick));
                });
            if (partner != owner_sidecar->captive_creatures.end()) {
                const int64_t gestation_end = saturating_tick_add(
                    saturating_tick(source_tick),
                    std::max<uint64_t>(1, config_.captive_gestation_ticks));
                const int64_t cooldown_end = saturating_tick_add(
                    saturating_tick(source_tick),
                    std::max<uint64_t>(1, config_.captive_breed_cooldown_ticks));
                found->is_pregnant = true;
                found->gestation_end_tick = gestation_end;
                found->partner_species_id = partner->species_id;
                found->breed_cooldown_until = cooldown_end;
                partner->breed_cooldown_until = cooldown_end;
                result.breeding_started = true;
                result.partner_entity_id = partner->runtime_id;
                result.gestation_end_tick = static_cast<uint64_t>(gestation_end);
                SNT_LOG_INFO("Captive breeding started: chunk='%s' mother=%llu partner=%llu end_tick=%llu",
                             owner_chunk.dimension_id.c_str(),
                             static_cast<unsigned long long>(found->runtime_id),
                             static_cast<unsigned long long>(partner->runtime_id),
                             static_cast<unsigned long long>(result.gestation_end_tick));
            }
        }
    }
    result.tame_progress = found->tame_progress;
    const GameCreaturePresentationState state = make_captive_state(owner_chunk, *found);
    known_captive_creatures_.insert_or_assign(captive_entity_id, state);
    if (result.breeding_started) {
        emit(GameCreaturePresentationEventKind::kBreedingStarted, source_tick, state);
    } else {
        emit(GameCreaturePresentationEventKind::kTamingProgressed, source_tick, state);
        if (result.became_tamed) {
            emit(GameCreaturePresentationEventKind::kTamed, source_tick, state);
        }
    }
    return result;
}

GameCreaturePresentationState GameWildCreatureSystem::make_wild_state(
    const GameEcosystemWildProxyRebalanceRequest& request,
    const GameEcosystemWildProxyPlan& plan, bool is_interactive) const {
    const CreatureSpeciesDef* species = species_catalog_ == nullptr
        ? nullptr
        : species_catalog_->get_species(plan.species_id);
    const std::array<float, 3> position = choose_spawn_position(request.chunk, plan.stable_id);
    const float health = species == nullptr || !std::isfinite(species->base_health)
        ? 1.0f
        : std::max(0.01f, species->base_health);
    return {
        .entity_id = plan.stable_id,
        .chunk = request.chunk,
        .species_id = plan.species_id,
        .role = plan.role,
        .age_stage = CreatureAgeStage::ADULT,
        .position_x = position[0],
        .position_y = position[1],
        .position_z = position[2],
        .health = health,
        .is_interactive = is_interactive,
    };
}

std::array<float, 3> GameWildCreatureSystem::choose_spawn_position(
    const ChunkKey& chunk, uint64_t stable_id) const {
    constexpr int32_t kChunkSize = VoxelChunk::kChunkSize;
    const float base_x = static_cast<float>(chunk.chunk_x * kChunkSize);
    const float base_y = static_cast<float>(chunk.chunk_y * kChunkSize);
    const float base_z = static_cast<float>(chunk.chunk_z * kChunkSize);
    const VoxelChunk* voxel_chunk = chunks_ == nullptr ? nullptr : chunks_->get_chunk(
        chunk.dimension_id, chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);
    if (voxel_chunk == nullptr || voxel_chunk->terrain.size_x <= 0 ||
        voxel_chunk->terrain.size_y <= 0 || voxel_chunk->terrain.size_z <= 0) {
        return {base_x + 0.5f, base_y + 1.0f, base_z + 0.5f};
    }

    const int local_x = static_cast<int>(stable_id %
        static_cast<uint64_t>(voxel_chunk->terrain.size_x));
    const int local_z = static_cast<int>((stable_id >> 16) %
        static_cast<uint64_t>(voxel_chunk->terrain.size_z));
    for (int local_y = voxel_chunk->terrain.size_y - 1; local_y >= 0; --local_y) {
        const TerrainCell& cell = voxel_chunk->terrain.cell_at(local_x, local_y, local_z);
        if (!cell.is_solid() && !cell.has_fluid()) continue;
        return {
            base_x + static_cast<float>(local_x) + 0.5f,
            base_y + static_cast<float>(local_y) + 1.0f,
            base_z + static_cast<float>(local_z) + 0.5f,
        };
    }
    return {base_x + static_cast<float>(local_x) + 0.5f, base_y + 1.0f,
            base_z + static_cast<float>(local_z) + 0.5f};
}

uint64_t GameWildCreatureSystem::stable_captive_runtime_id(
    const ChunkKey& chunk, uint16_t species_id, size_t slot) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    hash_string(hash, chunk.dimension_id);
    hash_uint32(hash, static_cast<uint32_t>(chunk.chunk_x));
    hash_uint32(hash, static_cast<uint32_t>(chunk.chunk_y));
    hash_uint32(hash, static_cast<uint32_t>(chunk.chunk_z));
    hash_uint32(hash, species_id);
    hash_uint32(hash, static_cast<uint32_t>(slot));
    hash_byte(hash, 0xc3);
    return hash == 0 ? 1 : hash;
}

bool GameWildCreatureSystem::advance_captive_wander(
    const ChunkKey& chunk, CaptiveCreature& captive, uint64_t source_tick) const {
    if (config_.captive_wander_interval_ticks == 0 || !has_valid_pen_bounds(captive)) {
        return false;
    }

    const int64_t current_tick = saturating_tick(source_tick);
    const int32_t position_x = clamp_to_range(
        bounded_floor(captive.pos_x, captive.bounds_min_x),
        captive.bounds_min_x, captive.bounds_max_x);
    const int32_t position_y = clamp_to_range(
        bounded_floor(captive.pos_y, captive.bounds_min_y),
        captive.bounds_min_y, captive.bounds_max_y);
    const int32_t position_z = clamp_to_range(
        bounded_floor(captive.pos_z, captive.bounds_min_z),
        captive.bounds_min_z, captive.bounds_max_z);
    const int32_t target_x = clamp_to_range(
        bounded_floor(captive.wander_target_x, position_x),
        captive.bounds_min_x, captive.bounds_max_x);
    const int32_t target_y = clamp_to_range(
        bounded_floor(captive.wander_target_y, position_y),
        captive.bounds_min_y, captive.bounds_max_y);
    const int32_t target_z = clamp_to_range(
        bounded_floor(captive.wander_target_z, position_z),
        captive.bounds_min_z, captive.bounds_max_z);
    const bool has_valid_target = std::isfinite(captive.wander_target_x) &&
        std::isfinite(captive.wander_target_y) && std::isfinite(captive.wander_target_z) &&
        is_captive_cell_walkable(chunk, target_x, target_y, target_z);
    bool target_ready = has_valid_target;
    if (!target_ready || captive.next_wander_tick <= current_tick) {
        target_ready = choose_captive_wander_target(chunk, captive, source_tick);
    }

    const float prior_x = captive.pos_x;
    const float prior_y = captive.pos_y;
    const float prior_z = captive.pos_z;
    captive.pos_x = clamp_to_pen(captive.pos_x, captive.bounds_min_x, captive.bounds_max_x);
    captive.pos_y = clamp_to_pen(captive.pos_y, captive.bounds_min_y, captive.bounds_max_y);
    captive.pos_z = clamp_to_pen(captive.pos_z, captive.bounds_min_z, captive.bounds_max_z);
    if (!target_ready) {
        return captive.pos_x != prior_x || captive.pos_y != prior_y ||
            captive.pos_z != prior_z;
    }

    const float delta_x = captive.wander_target_x - captive.pos_x;
    const float delta_z = captive.wander_target_z - captive.pos_z;
    const float distance = std::sqrt(delta_x * delta_x + delta_z * delta_z);
    const CreatureSpeciesDef* const species = species_catalog_ == nullptr
        ? nullptr
        : species_catalog_->get_species(captive.species_id);
    const float move_speed = species != nullptr && valid_positive(species->move_speed)
        ? species->move_speed
        : config_.captive_fallback_move_speed;
    if (!valid_positive(move_speed) || distance <= kCaptiveTargetReachedDistance) {
        if (distance <= kCaptiveTargetReachedDistance) {
            captive.pos_x = captive.wander_target_x;
            captive.pos_z = captive.wander_target_z;
            captive.next_wander_tick = current_tick;
        }
        return captive.pos_x != prior_x || captive.pos_y != prior_y ||
            captive.pos_z != prior_z;
    }

    const float step = std::min(move_speed, distance);
    const float candidate_x = clamp_to_pen(
        captive.pos_x + delta_x / distance * step,
        captive.bounds_min_x, captive.bounds_max_x);
    const float candidate_z = clamp_to_pen(
        captive.pos_z + delta_z / distance * step,
        captive.bounds_min_z, captive.bounds_max_z);
    const int32_t candidate_block_x = clamp_to_range(
        bounded_floor(candidate_x, position_x), captive.bounds_min_x, captive.bounds_max_x);
    const int32_t candidate_block_y = clamp_to_range(
        bounded_floor(captive.pos_y, position_y), captive.bounds_min_y, captive.bounds_max_y);
    const int32_t candidate_block_z = clamp_to_range(
        bounded_floor(candidate_z, position_z), captive.bounds_min_z, captive.bounds_max_z);
    if (!is_captive_cell_walkable(chunk, candidate_block_x, candidate_block_y,
                                  candidate_block_z)) {
        // The pen may have changed since capture. Re-evaluate a deterministic
        // target on the next tick instead of walking through the new obstacle.
        captive.next_wander_tick = current_tick;
        return captive.pos_x != prior_x || captive.pos_y != prior_y ||
            captive.pos_z != prior_z;
    }
    captive.pos_x = candidate_x;
    captive.pos_z = candidate_z;
    return captive.pos_x != prior_x || captive.pos_y != prior_y ||
        captive.pos_z != prior_z;
}

bool GameWildCreatureSystem::choose_captive_wander_target(
    const ChunkKey& chunk, CaptiveCreature& captive, uint64_t source_tick) const {
    if (!has_valid_pen_bounds(captive) || config_.captive_wander_interval_ticks == 0) {
        return false;
    }
    const int64_t current_tick = saturating_tick(source_tick);
    const int32_t current_x = clamp_to_range(
        bounded_floor(captive.pos_x, captive.bounds_min_x),
        captive.bounds_min_x, captive.bounds_max_x);
    const int32_t current_y = clamp_to_range(
        bounded_floor(captive.pos_y, captive.bounds_min_y),
        captive.bounds_min_y, captive.bounds_max_y);
    const int32_t current_z = clamp_to_range(
        bounded_floor(captive.pos_z, captive.bounds_min_z),
        captive.bounds_min_z, captive.bounds_max_z);
    const CreatureSpeciesDef* const species = species_catalog_ == nullptr
        ? nullptr
        : species_catalog_->get_species(captive.species_id);
    const int64_t pen_extent = std::max<int64_t>(
        static_cast<int64_t>(captive.bounds_max_x) - captive.bounds_min_x,
        static_cast<int64_t>(captive.bounds_max_z) - captive.bounds_min_z);
    const float configured_radius = species != nullptr && valid_positive(species->wander_radius)
        ? species->wander_radius
        : static_cast<float>(std::min<int64_t>(pen_extent, 8));
    const int64_t radius = std::clamp<int64_t>(
        static_cast<int64_t>(std::floor(std::max(1.0f, configured_radius))),
        1, std::max<int64_t>(1, pen_extent));
    const uint64_t span = static_cast<uint64_t>(radius * 2 + 1);
    const uint32_t attempts = std::max<uint32_t>(1, config_.captive_wander_target_attempts);
    uint64_t seed = captive_wander_seed(chunk, captive, source_tick);
    for (uint32_t attempt = 0; attempt < attempts; ++attempt) {
        seed = splitmix64(seed);
        const int64_t offset_x = static_cast<int64_t>(seed % span) - radius;
        seed = splitmix64(seed);
        const int64_t offset_z = static_cast<int64_t>(seed % span) - radius;
        const int32_t candidate_x = clamp_to_range(
            static_cast<int64_t>(current_x) + offset_x,
            captive.bounds_min_x, captive.bounds_max_x);
        const int32_t candidate_z = clamp_to_range(
            static_cast<int64_t>(current_z) + offset_z,
            captive.bounds_min_z, captive.bounds_max_z);
        if ((candidate_x == current_x && candidate_z == current_z) ||
            !is_captive_cell_walkable(chunk, candidate_x, current_y, candidate_z)) {
            continue;
        }
        captive.wander_target_x = static_cast<float>(candidate_x) + 0.5f;
        captive.wander_target_y = static_cast<float>(current_y);
        captive.wander_target_z = static_cast<float>(candidate_z) + 0.5f;
        captive.next_wander_tick = saturating_tick_add(
            current_tick, config_.captive_wander_interval_ticks);
        return true;
    }
    captive.next_wander_tick = saturating_tick_add(
        current_tick, config_.captive_wander_interval_ticks);
    return false;
}

bool GameWildCreatureSystem::is_captive_cell_walkable(
    const ChunkKey& chunk, int32_t block_x, int32_t block_y, int32_t block_z) const {
    if (chunks_ == nullptr || chunk.dimension_id.empty() ||
        block_y == std::numeric_limits<int32_t>::min()) {
        return false;
    }
    constexpr int32_t kChunkSize = VoxelChunk::kChunkSize;
    const auto cell_at = [this, &chunk](int32_t x, int32_t y, int32_t z)
        -> const TerrainCell* {
        const VoxelChunk* const voxel_chunk = chunks_->get_chunk(
            chunk.dimension_id, floor_divide(x, kChunkSize), floor_divide(y, kChunkSize),
            floor_divide(z, kChunkSize));
        if (voxel_chunk == nullptr) return nullptr;
        const int32_t local_x = local_coordinate(x, kChunkSize);
        const int32_t local_y = local_coordinate(y, kChunkSize);
        const int32_t local_z = local_coordinate(z, kChunkSize);
        if (!voxel_chunk->terrain.is_valid_cell(local_x, local_y, local_z)) return nullptr;
        return &voxel_chunk->terrain.cell_at(local_x, local_y, local_z);
    };
    const TerrainCell* const space = cell_at(block_x, block_y, block_z);
    const TerrainCell* const floor = cell_at(block_x, block_y - 1, block_z);
    return space != nullptr && floor != nullptr && !space->is_solid() && !space->has_fluid() &&
        floor->is_solid();
}

bool GameWildCreatureSystem::same_pen(
    const CaptiveCreature& left, const CaptiveCreature& right) noexcept {
    return left.bounds_min_x == right.bounds_min_x && left.bounds_min_y == right.bounds_min_y &&
        left.bounds_min_z == right.bounds_min_z && left.bounds_max_x == right.bounds_max_x &&
        left.bounds_max_y == right.bounds_max_y && left.bounds_max_z == right.bounds_max_z;
}

bool GameWildCreatureSystem::has_valid_pen_bounds(const CaptiveCreature& creature) noexcept {
    return creature.bounds_min_x <= creature.bounds_max_x &&
        creature.bounds_min_y <= creature.bounds_max_y &&
        creature.bounds_min_z <= creature.bounds_max_z;
}

bool GameWildCreatureSystem::is_breedable(
    const CaptiveCreature& creature, int64_t current_tick) noexcept {
    return creature.is_tamed && creature.age_stage == CreatureAgeStage::ADULT &&
        !creature.is_pregnant && valid_positive(creature.health) &&
        creature.breed_cooldown_until <= current_tick && has_valid_pen_bounds(creature);
}

bool GameWildCreatureSystem::should_emit_captive_update(uint64_t source_tick) const noexcept {
    return config_.captive_presentation_interval_ticks == 0 ||
        source_tick % config_.captive_presentation_interval_ticks == 0;
}

bool GameWildCreatureSystem::is_inside_pen(
    const GameCreaturePresentationState& creature,
    const GameCreaturePenBounds& bounds) noexcept {
    return creature.position_x >= static_cast<float>(bounds.min_x) &&
           creature.position_x <= static_cast<float>(bounds.max_x) + 1.0f &&
           creature.position_y >= static_cast<float>(bounds.min_y) &&
           creature.position_y <= static_cast<float>(bounds.max_y) + 1.0f &&
           creature.position_z >= static_cast<float>(bounds.min_z) &&
           creature.position_z <= static_cast<float>(bounds.max_z) + 1.0f;
}

GameCreaturePresentationState GameWildCreatureSystem::make_captive_state(
    const ChunkKey& chunk, const CaptiveCreature& creature) const {
    return {
        .entity_id = creature.runtime_id,
        .chunk = chunk,
        .species_id = creature.species_id,
        .role = creature.role,
        .age_stage = creature.age_stage,
        .position_x = creature.pos_x,
        .position_y = creature.pos_y,
        .position_z = creature.pos_z,
        .health = creature.health,
        .is_captive = true,
        .is_tamed = creature.is_tamed,
    };
}

void GameWildCreatureSystem::emit(
    GameCreaturePresentationEventKind kind, uint64_t source_tick,
    const GameCreaturePresentationState& creature) const {
    if (presentation_sink_ == nullptr) return;
    presentation_sink_->on_creature_presentation_event({
        .kind = kind,
        .source_tick = source_tick,
        .creature = creature,
    });
}

}  // namespace snt::game
