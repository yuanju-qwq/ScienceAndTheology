// Durable machine work-order contract for AE pattern providers.
//
// A provider owns the request identity while the target machine owns the
// actual execution state.  Stable ResourceContentStack values cross chunk
// persistence; the matching compact ResourceStack values are used only by
// the live machine tick.  This keeps provider dispatch from bypassing normal
// machine energy, duration, output-capacity, or reload behavior.

#pragma once

#include "game/resources/resource_key.h"
#include "game/world/defs/entity_data.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snt::game {

enum class MachineAutomationWorkOrderState : uint8_t {
    kQueued = 0,
    kRunning = 1,
    kOutputReady = 2,
    kFailed = 3,
};

// The pair is durable because provider anchors are world-owned EntityIds and
// serials are allocated by that provider's sidecar owner.  No process-local
// counter or ECS handle crosses a save/reload boundary.
struct MachineAutomationWorkOrderIdentity {
    EntityId provider_anchor_entity_id;
    uint64_t provider_job_serial = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return provider_anchor_entity_id.is_valid() && provider_job_serial != 0;
    }

    friend bool operator==(const MachineAutomationWorkOrderIdentity&,
                           const MachineAutomationWorkOrderIdentity&) = default;
};

// Chunk-sidecar representation.  It contains only stable content identities.
struct MachineAutomationWorkOrderRecord {
    MachineAutomationWorkOrderIdentity identity;
    std::string recipe_id;
    std::vector<ResourceContentStack> expected_outputs;
    MachineAutomationWorkOrderState state = MachineAutomationWorkOrderState::kQueued;
};

// Live machine representation.  Expected outputs are compact values tied to
// MachineRuntimeComponent::resource_runtime_index.
struct MachineAutomationWorkOrder {
    MachineAutomationWorkOrderIdentity identity;
    std::string recipe_id;
    std::vector<ResourceStack> expected_outputs;
    MachineAutomationWorkOrderState state = MachineAutomationWorkOrderState::kQueued;
};

}  // namespace snt::game
