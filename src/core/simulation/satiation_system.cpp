#include "simulation/satiation_system.hpp"

namespace science_and_theology {

// ============================================================
// Initialization
// ============================================================

void SatiationSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
    last_hunger_level_ = HungerLevel::SATIATED;
    last_source_essence_total_ = 0.0f;
}

// ============================================================
// Tick (active chunks)
// ============================================================

void SatiationSystem::tick_active(const ChunkKey& chunk, float delta,
                                  const TickContext* ctx) {
    if (!satiation_data_) return;

    float old_satiation = satiation_data_->satiation_current();
    HungerLevel old_level = satiation_data_->hunger_level();
    float old_essence = satiation_data_->get_total_source_essence();

    HungerLevel new_level = satiation_data_->tick();

    float new_satiation = satiation_data_->satiation_current();
    float new_essence = satiation_data_->get_total_source_essence();

    // Emit satiation_changed if value changed
    if (event_bus_ && new_satiation != old_satiation) {
        event_bus_->emit(GameEvent::satiation_changed(
            player_id_, old_satiation, new_satiation));
    }

    // Emit hunger_level_changed if level changed
    if (event_bus_ && new_level != old_level) {
        event_bus_->emit(GameEvent::hunger_level_changed(
            player_id_,
            static_cast<int>(old_level),
            static_cast<int>(new_level)));
        last_hunger_level_ = new_level;
    }

    // Emit source_essence_changed if total changed
    if (event_bus_ && new_essence != old_essence) {
        event_bus_->emit(GameEvent::source_essence_changed(
            player_id_, old_essence, new_essence));
        last_source_essence_total_ = new_essence;
    }
}

// ============================================================
// Tick (sleeping chunks) — no-op for satiation
// ============================================================

void SatiationSystem::tick_sleeping(const ChunkKey& chunk, float delta,
                                    const TickContext* ctx) {
    // Satiation is player-level, not chunk-level.
    // Sleeping chunks do not affect satiation decay.
}

// ============================================================
// Shutdown
// ============================================================

void SatiationSystem::shutdown() {
    satiation_data_ = nullptr;
}

// ============================================================
// Setters
// ============================================================

void SatiationSystem::set_satiation_data(SatiationData* data) {
    satiation_data_ = data;
    if (data) {
        last_hunger_level_ = data->hunger_level();
        last_source_essence_total_ = data->get_total_source_essence();
    }
}

void SatiationSystem::set_player_id(uint64_t player_id) {
    player_id_ = player_id;
}

} // namespace science_and_theology
