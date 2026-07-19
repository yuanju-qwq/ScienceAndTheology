// Dedicated-server shared-world interaction transactions.
//
// This module implements the selected co-op model: clients provide local
// raycast/prediction and machine prerequisite hints, while the host owns the
// final terrain, sidecar, inventory, and persistence-visible mutation. It is
// main-thread-only and deliberately does not perform anti-cheat-style replay
// of client visibility, mining speed, or machine prerequisite checks.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snt::ecs {
class World;
}

namespace snt::voxel {
class ChunkRegistry;
}

namespace snt::game {
class GameChunkSidecarRegistry;
class GameContentRegistry;
class MachineInteractionService;
}

namespace snt::game::replication {

class GameServerPlayerBedService;
class GameServerPlayerState;
class IGameServerPlayerStateCheckpointSink;
class GameServerInventoryReplication;

// A server-owned content catalog entry. Clients select item ids, but do not
// get to write arbitrary terrain material or flag combinations into saves.
struct GameServerBlockDefinition {
    std::string item_id;
    uint32_t material_id = 0;
    uint32_t placement_flags = 0;
    bool is_bed = false;
};

struct GameServerPlayerInteractionConfig {
    uint32_t air_material_id = 0;
    uint32_t reserved_grave_material_id = 255;
    std::vector<GameServerBlockDefinition> block_definitions;
};

enum class GameServerPlayerInteractionEventKind : uint8_t {
    kBlockMined,
    kBlockPlaced,
    kMachinePlaced,
    kBedUsed,
    kMachineActivated,
    kMachineOutputCollected,
};

// Value-only event seam for task producers, chunk replication, UI notices,
// and future Steam achievements. It never gives a consumer world ownership.
struct GameServerPlayerInteractionEvent {
    GameServerPlayerInteractionEventKind kind =
        GameServerPlayerInteractionEventKind::kBlockMined;
    std::string account_id;
    uint64_t tick_index = 0;
    GameBlockInteractionCommand command;
    std::string item_id;
    std::string machine_id;
    uint32_t previous_material = 0;
    uint32_t current_material = 0;
};

class IGameServerPlayerInteractionEventSink {
public:
    virtual ~IGameServerPlayerInteractionEventSink() = default;
    virtual void on_player_interaction(const GameServerPlayerInteractionEvent& event) = 0;
};

// Command intake depends on this narrow service contract, not on terrain,
// ECS, or persistence implementation details.
class IGameServerPlayerInteractionService {
public:
    virtual ~IGameServerPlayerInteractionService() = default;

    virtual snt::core::Expected<void> apply_block_interaction(
        const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
        uint64_t tick_index) = 0;
    virtual snt::core::Expected<void> submit_machine_input_slot_transfer(
        const GameAuthenticatedPeer& peer,
        const GameMachineInputSlotTransferCommand& command,
        uint64_t tick_index) = 0;
};

class GameServerPlayerInteractionService final
    : public IGameServerPlayerInteractionService {
public:
    [[nodiscard]] static snt::core::Expected<
        std::unique_ptr<GameServerPlayerInteractionService>>
    create(snt::ecs::World& world, snt::voxel::ChunkRegistry& chunks,
           GameChunkSidecarRegistry& sidecars, GameServerPlayerState& player_state,
           GameServerPlayerBedService& beds, const GameContentRegistry& content,
           MachineInteractionService& machine_interactions,
           GameServerInventoryReplication* inventory_replication = nullptr,
           IGameServerPlayerStateCheckpointSink* checkpoint_sink = nullptr,
           std::vector<IGameServerPlayerInteractionEventSink*> event_sinks = {},
           GameServerPlayerInteractionConfig config = {});

    GameServerPlayerInteractionService(const GameServerPlayerInteractionService&) = delete;
    GameServerPlayerInteractionService& operator=(const GameServerPlayerInteractionService&) = delete;

    [[nodiscard]] snt::core::Expected<void> apply_block_interaction(
        const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
        uint64_t tick_index) override;
    [[nodiscard]] snt::core::Expected<void> submit_machine_input_slot_transfer(
        const GameAuthenticatedPeer& peer,
        const GameMachineInputSlotTransferCommand& command,
        uint64_t tick_index) override;

    [[nodiscard]] static std::vector<GameServerBlockDefinition> default_block_definitions();

private:
    GameServerPlayerInteractionService(
        snt::ecs::World& world, snt::voxel::ChunkRegistry& chunks,
        GameChunkSidecarRegistry& sidecars, GameServerPlayerState& player_state,
        GameServerPlayerBedService& beds, const GameContentRegistry& content,
        MachineInteractionService& machine_interactions,
        GameServerInventoryReplication* inventory_replication,
        IGameServerPlayerStateCheckpointSink* checkpoint_sink,
        std::vector<IGameServerPlayerInteractionEventSink*> event_sinks,
        GameServerPlayerInteractionConfig config);

    [[nodiscard]] snt::core::Expected<void> apply_mine(
        const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
        uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> apply_place(
        const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
        uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> apply_machine_place(
        const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
        uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> apply_use(
        const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
        uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> apply_machine_activation(
        const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
        uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> apply_machine_collect(
        const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
        uint64_t tick_index);
    [[nodiscard]] snt::core::Expected<void> apply_machine_input_slot_transfer(
        const GameAuthenticatedPeer& peer,
        const GameMachineInputSlotTransferCommand& command,
        uint64_t tick_index);

    [[nodiscard]] const GameServerBlockDefinition* find_block_by_item(
        const std::string& item_id) const noexcept;
    [[nodiscard]] const GameServerBlockDefinition* find_block_by_material(
        uint32_t material_id) const noexcept;
    [[nodiscard]] snt::core::Expected<void> mark_player_state_dirty(
        const GameAuthenticatedPeer& peer);
    void emit_event(GameServerPlayerInteractionEvent event) const;

    snt::ecs::World* world_ = nullptr;
    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    GameServerPlayerState* player_state_ = nullptr;
    GameServerPlayerBedService* beds_ = nullptr;
    const GameContentRegistry* content_ = nullptr;
    MachineInteractionService* machine_interactions_ = nullptr;
    GameServerInventoryReplication* inventory_replication_ = nullptr;
    IGameServerPlayerStateCheckpointSink* checkpoint_sink_ = nullptr;
    std::vector<IGameServerPlayerInteractionEventSink*> event_sinks_;
    GameServerPlayerInteractionConfig config_;
};

}  // namespace snt::game::replication
