// Value-only creature presentation contract.
//
// Simulation owns when these values change; client presentation and network
// replication only consume them. Keeping this contract in world definitions
// prevents either consumer from depending on wildlife AI or renderer handles.

#pragma once

#include "game/world/defs/creature_species.h"
#include "game/world/voxel_primitives.h"

#include <cstdint>

namespace snt::game {

struct GameCreaturePresentationState {
    uint64_t entity_id = 0;
    ChunkKey chunk;
    uint16_t species_id = 0;
    CreatureRole role = CreatureRole::HERBIVORE;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    float health = 1.0f;
    // A far visual representative is presentation-only. Only an interactive
    // representative can be passed to authoritative wild-creature actions.
    bool is_interactive = false;
    bool is_captive = false;
    bool is_tamed = false;
};

enum class GameCreaturePresentationEventKind : uint8_t {
    kSpawned,
    kDespawned,
    kDamaged,
    kKilled,
    kCaptured,
    kTamingProgressed,
    kTamed,
};

struct GameCreaturePresentationEvent {
    GameCreaturePresentationEventKind kind = GameCreaturePresentationEventKind::kSpawned;
    uint64_t source_tick = 0;
    GameCreaturePresentationState creature;
};

class IGameCreaturePresentationSink {
public:
    virtual ~IGameCreaturePresentationSink() = default;
    virtual void on_creature_presentation_event(
        const GameCreaturePresentationEvent& event) = 0;
};

}  // namespace snt::game
