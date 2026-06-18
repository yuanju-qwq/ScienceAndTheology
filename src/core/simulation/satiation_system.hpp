#pragma once

#include "simulation_system.hpp"
#include "event_bus.hpp"
#include "../player/satiation_data.hpp"

namespace science_and_theology {

class WorldData;

// ============================================================
// SatiationSystem — simulation subsystem for player satiation
// ============================================================
//
// Ticks the player's SatiationData each game tick, emitting events
// when satiation value or hunger level changes.
//
// Priority: 8 (after Season=6, TreeGrowth=7).
// Thread safety: false (modifies player data, not chunk-parallel).
//
// Integration note:
// - The SatiationData instance is owned externally (e.g. by a
//   player controller or a future PlayerData aggregate).
// - Call set_satiation_data() before the first tick.
// - FoodRegistry (future): will map ItemId -> food_value for
//   automatic food consumption. Currently food_value is passed
//   directly via SatiationData::eat().

class SatiationSystem : public SimulationSystem {
public:
    SatiationSystem() = default;
    ~SatiationSystem() override = default;

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta) override;
    void tick_sleeping(const ChunkKey& chunk, float delta) override;
    void shutdown() override;

    const char* name() const override { return "SatiationSystem"; }
    int priority() const override { return 8; }
    bool is_thread_safe() const override { return false; }

    // Set the satiation data to tick. Must be called before first tick.
    void set_satiation_data(SatiationData* data);
    SatiationData* satiation_data() const { return satiation_data_; }

    // Set the player id used in emitted events.
    void set_player_id(uint64_t player_id);
    uint64_t player_id() const { return player_id_; }

private:
    SatiationData* satiation_data_ = nullptr;
    uint64_t player_id_ = 0;
    HungerLevel last_hunger_level_ = HungerLevel::SATIATED;
    float last_source_essence_total_ = 0.0f;
};

} // namespace science_and_theology
