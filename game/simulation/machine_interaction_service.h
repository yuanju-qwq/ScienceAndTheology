// Game-owned manual-machine interaction boundary.
//
// Player commands resolve actor authority, range, inventory, tools, and
// world observations before calling this service on the simulation main
// thread. The service owns the stable machine-state transition and never
// accepts raw client network payloads or ECS pointers from worker jobs.

#pragma once

#include "core/expected.h"
#include "ecs/entity_guid.h"

#include <string>
#include <vector>

namespace snt::ecs {
class World;
}

namespace snt::game {

class GameContentRegistry;

// Value snapshot produced by an authoritative player-command implementation.
// structure_is_valid is normally derived by MachineStructureValidator from
// the machine anchor and current ChunkRegistry. This type deliberately
// contains no player entity, inventory, world, or transport pointer, so this
// gameplay service remains reusable by client and server simulation hosts.
struct MachineActivationContext {
    bool target_is_reachable = false;
    bool cover_is_present = false;
    bool ignition_is_present = false;
    bool structure_is_valid = false;
    std::vector<std::string> held_tool_tags;
};

class MachineInteractionService final {
public:
    explicit MachineInteractionService(GameContentRegistry& content_registry) noexcept;

    // Queues one manual activation after checking the current machine state
    // and its content-owned prerequisites. The subsequent worker tick still
    // rechecks inputs and output capacity before reserving anything.
    [[nodiscard]] snt::core::Expected<void> request_manual_activation(
        snt::ecs::World& world,
        snt::ecs::EntityGuid machine_guid,
        const MachineActivationContext& context);

private:
    GameContentRegistry& content_registry_;
};

}  // namespace snt::game
