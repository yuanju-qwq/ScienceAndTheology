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

void hash_byte(uint64_t& hash, uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_uint32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t byte_index = 0; byte_index < 4; ++byte_index) {
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

    std::set<uint64_t> current_ids;
    for (size_t slot = 0; slot < sidecar->captive_creatures.size(); ++slot) {
        CaptiveCreature& captive = sidecar->captive_creatures[slot];
        if (captive.runtime_id == 0) {
            captive.runtime_id = stable_captive_runtime_id(request.chunk, captive.species_id, slot);
        }
        const GameCreaturePresentationState state = make_captive_state(request.chunk, captive);
        current_ids.insert(captive.runtime_id);
        const auto [known, inserted] = known_captive_creatures_.insert_or_assign(
            captive.runtime_id, state);
        static_cast<void>(known);
        if (inserted) {
            emit(GameCreaturePresentationEventKind::kSpawned, request.source_tick, state);
        }
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
    CaptiveCreature* found = nullptr;
    sidecars_->for_each([&](const ChunkKey& chunk, GameChunkSidecar& sidecar) {
        if (found != nullptr || !sidecar.has_captive_creatures) return;
        for (CaptiveCreature& creature : sidecar.captive_creatures) {
            if (creature.runtime_id == captive_entity_id) {
                owner_chunk = chunk;
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
    }
    result.tame_progress = found->tame_progress;
    const GameCreaturePresentationState state = make_captive_state(owner_chunk, *found);
    known_captive_creatures_.insert_or_assign(captive_entity_id, state);
    emit(GameCreaturePresentationEventKind::kTamingProgressed, source_tick, state);
    if (result.became_tamed) emit(GameCreaturePresentationEventKind::kTamed, source_tick, state);
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
