// ScienceAndTheology graphical session implementation.

#define SNT_LOG_CHANNEL "game.client_session"
#include "science_and_theology_session.h"

#include "game/client/creature_presentation_world.h"
#include "game/client/day_night_lighting.h"
#include "assets/asset_manager.h"
#include "core/error.h"
#include "core/events.h"
#include "core/log.h"
#include "render/render_components.h"
#include "ecs/entity_guid.h"
#include "ecs/event_bus.h"
#include "ecs/world.h"
#include "engine/client_services.h"
#include "engine/simulation_services.h"
#include "game/network/game_inventory_replication.h"
#include "game/network/game_quest_book_replication.h"
#include "game/player/player_replication.h"
#include "game/simulation/ecosystem_system.h"
#include "game/world/defs/machine_structure_validator.h"
#include "game/worldgen/world_gen_config.h"
#include "network/tcp_udp_transport.h"
#include "player/player_controller.h"
#include "player/player_physics_system.h"
#include "player/ray_cast.h"
#include "player/voxel_collision.h"
#include "scene/scene.h"
#include "script/script_manager.h"
#include "ui/retained_mui_arc.h"
#include "voxel/chunk_render_system.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace snt::game {
namespace {

constexpr uint64_t kMovementInputHeartbeatTicks = 4;
constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] std::optional<int32_t> floor_to_block_coordinate(float value) noexcept {
    if (!std::isfinite(value) ||
        value < static_cast<float>(std::numeric_limits<int32_t>::min()) ||
        value > static_cast<float>(std::numeric_limits<int32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<int32_t>(std::floor(value));
}

// Offline play has no server player-state service, so this adapter makes the
// local camera/player transform the authoritative ecology center. Networked
// clients intentionally do not install it; their dedicated server supplies
// the corresponding player centers to its own simulation session.
class ClientEcosystemInterestProvider final : public IGameEcosystemInterestProvider {
public:
    ClientEcosystemInterestProvider(const snt::ecs::World& world,
                                    snt::ecs::EntityGuid camera_guid,
                                    std::string dimension_id) noexcept
        : world_(&world), camera_guid_(camera_guid), dimension_id_(std::move(dimension_id)) {}

    void collect_ecosystem_interest_centers(
        uint64_t /*current_tick*/,
        std::vector<GameEcosystemInterestCenter>& out_centers) const override {
        if (world_ == nullptr || dimension_id_.empty()) return;
        const entt::entity camera = world_->find_entity_by_guid(camera_guid_);
        if (camera == entt::null ||
            !world_->registry().all_of<snt::render::Transform>(camera)) {
            return;
        }
        const snt::render::Transform& transform =
            world_->registry().get<snt::render::Transform>(camera);
        const auto block_x = floor_to_block_coordinate(transform.position[0]);
        const auto block_y = floor_to_block_coordinate(transform.position[1]);
        const auto block_z = floor_to_block_coordinate(transform.position[2]);
        if (!block_x.has_value() || !block_y.has_value() || !block_z.has_value()) return;

        constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
        out_centers.push_back({
            .chunk = {
                dimension_id_,
                snt::player::floor_div_i32(*block_x, kChunkSize),
                snt::player::floor_div_i32(*block_y, kChunkSize),
                snt::player::floor_div_i32(*block_z, kChunkSize),
            },
        });
    }

private:
    const snt::ecs::World* world_ = nullptr;
    snt::ecs::EntityGuid camera_guid_;
    std::string dimension_id_;
};

struct LocalTerrainCell {
    uint16_t material = 0;
    bool solid = false;
};

[[nodiscard]] std::optional<LocalTerrainCell> find_local_terrain_cell(
    const snt::voxel::ChunkRegistry& chunks, const std::string& dimension_id,
    int32_t block_x, int32_t block_y, int32_t block_z) {
    constexpr int32_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int32_t chunk_x = snt::player::floor_div_i32(block_x, kChunkSize);
    const int32_t chunk_y = snt::player::floor_div_i32(block_y, kChunkSize);
    const int32_t chunk_z = snt::player::floor_div_i32(block_z, kChunkSize);
    const snt::voxel::VoxelChunk* chunk = chunks.get_chunk(
        dimension_id, chunk_x, chunk_y, chunk_z);
    if (chunk == nullptr) return std::nullopt;

    const int32_t local_x = snt::player::positive_mod_i32(block_x, kChunkSize);
    const int32_t local_y = snt::player::positive_mod_i32(block_y, kChunkSize);
    const int32_t local_z = snt::player::positive_mod_i32(block_z, kChunkSize);
    if (!chunk->terrain.is_valid_cell(local_x, local_y, local_z)) return std::nullopt;
    const snt::voxel::TerrainCell& cell = chunk->terrain.cell_at(local_x, local_y, local_z);
    return LocalTerrainCell{
        .material = static_cast<uint16_t>(cell.material),
        .solid = cell.is_solid(),
    };
}

[[nodiscard]] snt::player::Vec3 camera_look_direction(
    const snt::render::Transform& transform) noexcept {
    const float yaw = transform.rotation[1] * kPi / 180.0f;
    const float pitch = transform.rotation[0] * kPi / 180.0f;
    const float cos_pitch = std::cos(pitch);
    return {
        .x = std::cos(yaw) * cos_pitch,
        .y = std::sin(pitch),
        .z = std::sin(yaw) * cos_pitch,
    };
}

[[nodiscard]] bool has_collectible_machine_output(
    const replication::GameRemoteMachineState& machine) noexcept {
    return std::any_of(machine.machine.output_slots.begin(), machine.machine.output_slots.end(),
                       [](const replication::GameReplicatedMachineItemStack& output) {
                           return !output.item_id.empty() && output.count > 0;
                       });
}

[[nodiscard]] MachinePanelState make_machine_panel_state(
    const replication::GameRemoteMachineState& remote_machine,
    uint16_t expected_material) {
    const replication::GameReplicatedMachineState& machine = remote_machine.machine;
    MachinePanelState state{
        .dimension_id = machine.anchor_chunk.dimension_id,
        .root_x = machine.root_x,
        .root_y = machine.root_y,
        .root_z = machine.root_z,
        .expected_material = expected_material,
        .machine_id = machine.machine_id,
        .max_input_slots = static_cast<int32_t>(machine.max_input_slots),
        .max_output_slots = static_cast<int32_t>(machine.max_output_slots),
        .stored_energy = machine.stored_energy,
        .energy_capacity = machine.energy_capacity,
        .progress_ticks = machine.progress_ticks,
        .active_recipe_duration_ticks = machine.active_recipe_duration_ticks,
    };
    state.input_slots.reserve(machine.input_slots.size());
    for (const replication::GameReplicatedMachineItemStack& stack : machine.input_slots) {
        state.input_slots.push_back({.item_key = stack.item_id, .count = stack.count});
    }
    state.output_slots.reserve(machine.output_slots.size());
    for (const replication::GameReplicatedMachineItemStack& stack : machine.output_slots) {
        state.output_slots.push_back({.item_key = stack.item_id, .count = stack.count});
    }
    return state;
}

[[nodiscard]] uint8_t make_machine_activation_hints(
    const GameContentRegistry& content, const GameClientInteractionConfig& config,
    const WorldGenConfigSnapshot* worldgen_config,
    const snt::voxel::ChunkRegistry& chunks,
    const GameClientBlockInteractionTarget& target,
    const replication::GameRemoteMachineState& machine,
    const std::string& selected_item_id) {
    const MachineDefinition* definition = content.find_machine(machine.machine.machine_id);
    if (definition == nullptr) return 0;

    const MachineActivationRequirements& requirements = definition->activation_requirements;
    uint8_t hints = 0;
    if (requirements.requires_cover) {
        const auto cover = find_local_terrain_cell(
            chunks, target.dimension_id, target.hit_x, target.hit_y + 1, target.hit_z);
        if (cover.has_value() && cover->solid) {
            hints |= replication::kGameBlockInteractionHintCover;
        }
    }
    if (requirements.requires_ignition && !config.ignition_item_id.empty() &&
        selected_item_id == config.ignition_item_id) {
        hints |= replication::kGameBlockInteractionHintIgnition;
    }
    if (requirements.requires_valid_structure && machine.machine.machine_id == "bloomery" &&
        worldgen_config != nullptr) {
        const TerrainMaterialDef* terrain_material = worldgen_config->find_material(
            static_cast<TerrainMaterialId>(target.hit_material));
        const MachinePlacementDefinition* placement = terrain_material == nullptr
            ? nullptr
            : content.find_machine_placement_by_material_key(terrain_material->key);
        if (placement != nullptr && placement->machine_id == machine.machine.machine_id &&
            MachineStructureValidator::validate_bloomery(
                chunks, target.dimension_id, target.hit_x, target.hit_y, target.hit_z,
                terrain_material->id).valid()) {
            hints |= replication::kGameBlockInteractionHintStructure;
        }
    }
    return hints;
}

[[nodiscard]] std::optional<GameClientBlockInteractionTarget::FarmingTarget>
make_farming_interaction_target(
    const GameContentRegistry& content, const GameClientInteractionConfig& config,
    const WorldGenConfigSnapshot* worldgen_config,
    const snt::voxel::ChunkRegistry& chunks,
    const GameClientBlockInteractionTarget& target,
    const std::string& selected_item_id) {
    if (worldgen_config == nullptr) return std::nullopt;
    const TerrainMaterialId hit_material = static_cast<TerrainMaterialId>(target.hit_material);
    const TerrainMaterialDef* hit = worldgen_config->find_material(hit_material);
    if (hit == nullptr) return std::nullopt;

    GameClientBlockInteractionTarget::FarmingTarget farming;
    if (hit_material == worldgen_config->roles.dirt && !hit->required_tool_tag.empty() &&
        content.item_matches_tool_requirement(selected_item_id, hit->required_tool_tag,
                                              hit->required_mining_level)) {
        farming.can_till = true;
    }

    if (hit_material == worldgen_config->runtime_ids.farmland &&
        worldgen_config->find_crop_by_seed(selected_item_id) != nullptr &&
        target.hit_y < std::numeric_limits<int32_t>::max()) {
        const int32_t planting_y = target.hit_y + 1;
        const auto above = find_local_terrain_cell(
            chunks, target.dimension_id, target.hit_x, planting_y, target.hit_z);
        if (above.has_value() && above->material == worldgen_config->roles.air) {
            farming.can_plant = true;
            farming.planting_x = target.hit_x;
            farming.planting_y = planting_y;
            farming.planting_z = target.hit_z;
            farming.planting_expected_material = above->material;
        }
    }

    bool is_crop = false;
    for (const CropSpeciesDef& crop : worldgen_config->crop_species) {
        for (const std::string& stage_material_key : crop.stage_material_keys) {
            const TerrainMaterialId stage_material = worldgen_config->material_id_or(
                stage_material_key, worldgen_config->roles.air);
            if (stage_material != worldgen_config->roles.air && stage_material == hit_material) {
                is_crop = true;
                break;
            }
        }
        if (is_crop) break;
    }
    if (is_crop) {
        farming.can_harvest = true;
        farming.can_fertilize = !config.fertilizer_item_id.empty() &&
            selected_item_id == config.fertilizer_item_id;
    }

    return farming.can_till || farming.can_plant || farming.can_fertilize ||
            farming.can_harvest
        ? std::optional<GameClientBlockInteractionTarget::FarmingTarget>{std::move(farming)}
        : std::nullopt;
}

class ReplicationBlockInteractionCommandSink final
    : public IGameClientBlockInteractionCommandSink {
public:
    ReplicationBlockInteractionCommandSink(replication::GameClientReplicationSession& session,
                                           uint64_t& next_sequence) noexcept
        : session_(&session), next_sequence_(&next_sequence) {}

    [[nodiscard]] snt::core::Expected<void> submit_block_interaction(
        replication::GameBlockInteractionCommand command) override {
        if (session_ == nullptr || next_sequence_ == nullptr || *next_sequence_ == 0 ||
            *next_sequence_ == std::numeric_limits<uint64_t>::max()) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "Client block interaction command sequence is unavailable"};
        }
        if (auto result = session_->enqueue_block_interaction(*next_sequence_, std::move(command));
            !result) {
            return result.error();
        }
        ++*next_sequence_;
        return {};
    }

private:
    replication::GameClientReplicationSession* session_ = nullptr;
    uint64_t* next_sequence_ = nullptr;
};

[[nodiscard]] GamePlayerItemStack to_replication_inventory_stack(
    const ItemStackState& stack) {
    return {
        .item_id = stack.item_key,
        .count = stack.count,
        .instance_data = stack.instance_data,
    };
}

[[nodiscard]] ItemStackState from_replication_inventory_stack(
    const GamePlayerItemStack& stack) {
    return {
        .item_key = stack.item_id,
        .count = stack.count,
        .instance_data = stack.instance_data,
    };
}

[[nodiscard]] std::vector<ItemStackState> from_replication_inventory_slots(
    const GamePlayerInventory& inventory) {
    std::vector<ItemStackState> slots;
    slots.reserve(inventory.slots.size());
    for (const GamePlayerItemStack& stack : inventory.slots) {
        slots.push_back(from_replication_inventory_stack(stack));
    }
    return slots;
}

// The retained UI can outlive a reconnect. It therefore follows the owning
// unique_ptr instead of retaining a raw session pointer that would dangle
// between LAN joins. It never predicts an inventory mutation locally.
class ReplicationInventorySlotTransferCommandSink final
    : public IInventorySlotTransferCommandSink {
public:
    ReplicationInventorySlotTransferCommandSink(
        std::unique_ptr<replication::GameClientReplicationSession>& session,
        uint64_t& next_sequence) noexcept
        : session_owner_(&session), next_sequence_(&next_sequence) {}

    [[nodiscard]] snt::core::Expected<void> submit_slot_transfer(
        InventorySlotTransferRequest request) override {
        replication::GameClientReplicationSession* const session =
            session_owner_ != nullptr ? session_owner_->get() : nullptr;
        if (session == nullptr || next_sequence_ == nullptr || *next_sequence_ == 0 ||
            *next_sequence_ == std::numeric_limits<uint64_t>::max()) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "Client inventory slot transfer command sequence is unavailable"};
        }
        if (request.expected_revision == 0) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "Client inventory is awaiting an authoritative snapshot"};
        }
        replication::GameInventorySlotTransferCommand command{
            .request_id = request.request_id,
            .expected_inventory_revision = request.expected_revision,
            .source_slot = request.source_slot,
            .target_slot = request.target_slot,
            .count = request.count,
            .expected_source = to_replication_inventory_stack(request.expected_source),
            .expected_target = to_replication_inventory_stack(request.expected_target),
        };
        if (auto result = session->enqueue_inventory_slot_transfer(
                *next_sequence_, std::move(command));
            !result) {
            return result.error();
        }
        ++*next_sequence_;
        return {};
    }

private:
    std::unique_ptr<replication::GameClientReplicationSession>* session_owner_ = nullptr;
    uint64_t* next_sequence_ = nullptr;
};

// Retained machine panels follow the same reconnect-safe session ownership
// rule as the player inventory adapter. They only serialize observed values;
// the host remains responsible for reach, machine anchoring, and both writes.
class ReplicationMachineInputSlotTransferCommandSink final
    : public IMachineInputSlotTransferCommandSink {
public:
    ReplicationMachineInputSlotTransferCommandSink(
        std::unique_ptr<replication::GameClientReplicationSession>& session,
        uint64_t& next_sequence) noexcept
        : session_owner_(&session), next_sequence_(&next_sequence) {}

    [[nodiscard]] snt::core::Expected<void> submit_machine_input_slot_transfer(
        MachineInputSlotTransferRequest request) override {
        replication::GameClientReplicationSession* const session =
            session_owner_ != nullptr ? session_owner_->get() : nullptr;
        if (session == nullptr || next_sequence_ == nullptr || *next_sequence_ == 0 ||
            *next_sequence_ == std::numeric_limits<uint64_t>::max()) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "Client machine input transfer command sequence is unavailable"};
        }
        if (request.expected_inventory_revision == 0) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "Client machine input transfer is awaiting an authoritative inventory snapshot"};
        }
        const auto direction = request.direction ==
                MachineInputSlotTransferDirection::PlayerToMachineInput
            ? replication::GameMachineInputSlotTransferDirection::kPlayerToMachineInput
            : replication::GameMachineInputSlotTransferDirection::kMachineInputToPlayer;
        replication::GameMachineInputSlotTransferCommand command{
            .request_id = request.request_id,
            .expected_inventory_revision = request.expected_inventory_revision,
            .direction = direction,
            .dimension_id = request.target.dimension_id,
            .root_x = request.target.root_x,
            .root_y = request.target.root_y,
            .root_z = request.target.root_z,
            .expected_material = request.target.expected_material,
            .player_slot = static_cast<uint16_t>(request.player_slot),
            .machine_input_slot = static_cast<uint16_t>(request.machine_input_slot),
            .count = request.count,
            .expected_player_slot = to_replication_inventory_stack(request.expected_player_slot),
            .expected_machine_input_slot =
                to_replication_inventory_stack(request.expected_machine_input_slot),
        };
        if (auto result = session->enqueue_machine_input_slot_transfer(
                *next_sequence_, std::move(command));
            !result) {
            return result.error();
        }
        ++*next_sequence_;
        return {};
    }

private:
    std::unique_ptr<replication::GameClientReplicationSession>* session_owner_ = nullptr;
    uint64_t* next_sequence_ = nullptr;
};

[[nodiscard]] bool same_movement_intent(
    const replication::GamePlayerMovementInput& left,
    const replication::GamePlayerMovementInput& right) noexcept {
    return left.forward_axis == right.forward_axis &&
           left.strafe_axis == right.strafe_axis &&
           left.flags == right.flags &&
           left.yaw_centidegrees == right.yaw_centidegrees &&
           left.pitch_centidegrees == right.pitch_centidegrees;
}

[[nodiscard]] float normalize_yaw(float yaw) noexcept {
    while (yaw < -180.0f) yaw += 360.0f;
    while (yaw >= 180.0f) yaw -= 360.0f;
    return yaw;
}

void append_cross_arm(snt::ui::Arc2DCommandBuffer& commands, float center_x, float center_y,
                      float offset_x, float offset_y, snt::ui::Color color) {
    constexpr float kArmLength = 8.0f;
    constexpr float kGap = 4.0f;
    constexpr float kThickness = 2.0f;
    commands.rect({.pos = {center_x - kGap - kArmLength + offset_x,
                           center_y - kThickness * 0.5f + offset_y},
                   .size = {kArmLength, kThickness}}, color);
    commands.rect({.pos = {center_x + kGap + offset_x,
                           center_y - kThickness * 0.5f + offset_y},
                   .size = {kArmLength, kThickness}}, color);
    commands.rect({.pos = {center_x - kThickness * 0.5f + offset_x,
                           center_y - kGap - kArmLength + offset_y},
                   .size = {kThickness, kArmLength}}, color);
    commands.rect({.pos = {center_x - kThickness * 0.5f + offset_x,
                           center_y + kGap + offset_y},
                   .size = {kThickness, kArmLength}}, color);
}

}  // namespace

ScienceAndTheologyClientSession::ScienceAndTheologyClientSession(
    GameSessionConfig config,
    std::shared_ptr<localization::LocalizationService> localization,
    std::optional<replication::GameClientAuthentication> connection_authentication)
    : config_(std::move(config)), block_interaction_controller_(config_.client_interaction),
      simulation_session_(config_),
      localization_(std::move(localization)),
      connection_authentication_(std::move(connection_authentication)) {
    if (connection_authentication_) {
        local_player_identity_ = connection_authentication_->local_identity;
    }
}

ScienceAndTheologyClientSession::~ScienceAndTheologyClientSession() { shutdown(); }

snt::core::Expected<void> ScienceAndTheologyClientSession::register_content(
    snt::engine::SimulationServices& services) {
    if (auto result = simulation_session_.register_content(services); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::register_content");
        return error;
    }
    services_ = &services;
    return {};
}

snt::core::Expected<void> ScienceAndTheologyClientSession::create_world(
    snt::engine::SimulationWorldSession& world_session) {
    if (auto result = simulation_session_.create_world(world_session); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::create_world");
        return error;
    }
    if (config_.client_network.lan_discovery_enabled) {
        if (!connection_authentication_) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "LAN browsing is enabled without injected player authentication"};
        }
        LanServerBrowserConfig browser_config;
        browser_config.discovery.target_address = config_.client_network.lan_discovery_address;
        browser_config.discovery.port = config_.client_network.lan_discovery_port;
        auto browser = LanServerBrowserModel::create(std::move(browser_config));
        if (!browser) {
            auto error = browser.error();
            error.with_context("ScienceAndTheologyClientSession::create_world(LAN browser)");
            return error;
        }
        (*browser)->set_server_password(connection_authentication_->server_password);
        lan_server_browser_ = std::move(*browser);
        SNT_LOG_INFO("LAN server browser initialized (address=%s port=%u)",
                     config_.client_network.lan_discovery_address.c_str(),
                     config_.client_network.lan_discovery_port);
    }

    if (!config_.client_network.enabled) return {};
    if (!connection_authentication_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Client networking is enabled without injected player authentication"};
    }
    auto connected = connect_tcp_udp(config_.client_network.host, config_.client_network.tcp_port,
                                     config_.client_network.udp_port,
                                     connection_authentication_->server_password);
    if (!connected) {
        auto error = connected.error();
        error.with_context("ScienceAndTheologyClientSession::create_world(client replication)");
        return error;
    }
    return {};
}

bool ScienceAndTheologyClientSession::uses_network_presentation() const noexcept {
    return config_.client_network.enabled || lan_server_browser_ != nullptr;
}

snt::core::Expected<void> ScienceAndTheologyClientSession::connect_tcp_udp(
    std::string host, uint16_t tcp_port, uint16_t udp_port, std::string server_password) {
    if (!connection_authentication_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Client replication requires injected player authentication"};
    }
    if (host.empty() || tcp_port == 0 || udp_port == 0) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Client replication requires a non-empty host and non-zero ports"};
    }

    if (replication_session_) {
        replication_session_->shutdown();
        replication_session_.reset();
        clear_remote_replication_state();
    }

    snt::network::TcpUdpConnectConfig connection_config;
    connection_config.host = host;
    connection_config.tcp_port = tcp_port;
    connection_config.udp_port = udp_port;
    replication::GameClientAuthentication authentication = *connection_authentication_;
    authentication.server_password = std::move(server_password);
    auto connection = replication::GameClientReplicationSession::connect_tcp_udp(
        std::move(connection_config), std::move(authentication));
    if (!connection) {
        auto error = connection.error();
        error.with_context("ScienceAndTheologyClientSession::connect_tcp_udp");
        return error;
    }

    replication_session_ = std::move(*connection);
    replication_disconnect_reported_ = false;
    ensure_remote_replication_state();
    SNT_LOG_INFO("Client replication connection requested (host=%s tcp=%u udp=%u)",
                 host.c_str(), tcp_port, udp_port);
    SNT_LOG_INFO("Client is awaiting authoritative player, inventory, and task-book snapshots");
    return {};
}

void ScienceAndTheologyClientSession::ensure_remote_replication_state() {
    const std::string account_id = local_player_identity_ ? local_player_identity_->account_id : std::string{};
    if (!remote_player_world_) {
        remote_player_world_ = std::make_unique<replication::GameRemotePlayerWorld>(account_id);
    }
    if (!remote_inventory_state_) {
        remote_inventory_state_ = std::make_unique<replication::GameClientInventoryState>(account_id);
    }
    if (!quest_book_state_) {
        quest_book_state_ = std::make_unique<replication::GameClientQuestBookState>(account_id);
    }
}

void ScienceAndTheologyClientSession::clear_remote_replication_state() {
    if (remote_chunk_world_) {
        remote_chunk_world_->clear();
        if (chunk_render_system_) {
            for (const snt::voxel::ChunkKey& key : remote_chunk_world_->drain_dirty_chunks()) {
                chunk_render_system_->mark_dirty(key);
            }
        }
    }
    if (remote_machine_world_) remote_machine_world_->clear();
    if (gameplay_ui_) gameplay_ui_->clear_machine_authority();
    if (remote_player_world_) remote_player_world_->clear();
    if (remote_inventory_state_) {
        remote_inventory_state_->clear();
        if (gameplay_ui_) gameplay_ui_->clear_inventory_authority();
    }
    if (quest_book_state_) quest_book_state_->clear();
    block_interaction_bindings_reported_ = false;
    block_interaction_submission_error_reported_ = false;
    network_crafting_unavailable_reported_ = false;
}

snt::core::Expected<void> ScienceAndTheologyClientSession::apply_remote_inventory_to_ui(
    uint64_t previous_inventory_revision, uint64_t previous_response_revision) {
    if (!remote_inventory_state_ || !gameplay_ui_) return {};
    const replication::GameInventorySnapshot* const snapshot = remote_inventory_state_->snapshot();
    if (snapshot == nullptr) return {};

    const bool inventory_changed = snapshot->inventory_revision != previous_inventory_revision;
    const bool response_changed = snapshot->response_revision != previous_response_revision;
    if (!inventory_changed && !response_changed) return {};

    const bool matching_pending_response = response_changed &&
        snapshot->response.request_id != 0 &&
        snapshot->response.kind == replication::GameInventoryCommandKind::kInventorySlotTransfer &&
        snapshot->response.request_id ==
            gameplay_ui_->pending_inventory_slot_transfer_request_id();
    if (matching_pending_response) {
        InventorySlotTransferConfirmation confirmation{
            .request_id = snapshot->response.request_id,
            .outcome = snapshot->response.outcome ==
                    replication::GameInventorySlotTransferOutcome::kAccepted
                ? InventorySlotTransferOutcome::Accepted
                : InventorySlotTransferOutcome::Rejected,
            .authoritative_revision = snapshot->inventory_revision,
            .slots = from_replication_inventory_slots(snapshot->inventory),
            .max_stack_size = snapshot->inventory.max_stack_size,
            .rejection_reason = snapshot->response.rejection_reason,
        };
        static_cast<void>(
            gameplay_ui_->apply_inventory_slot_transfer_confirmation(std::move(confirmation)));
        return {};
    }

    const bool matching_machine_response = response_changed &&
        snapshot->response.request_id != 0 &&
        snapshot->response.kind ==
            replication::GameInventoryCommandKind::kMachineInputSlotTransfer &&
        snapshot->response.request_id ==
            gameplay_ui_->pending_machine_input_slot_transfer_request_id();
    if (matching_machine_response) {
        MachineInputSlotTransferConfirmation confirmation{
            .request_id = snapshot->response.request_id,
            .outcome = snapshot->response.outcome ==
                    replication::GameInventorySlotTransferOutcome::kAccepted
                ? InventorySlotTransferOutcome::Accepted
                : InventorySlotTransferOutcome::Rejected,
            .authoritative_inventory_revision = snapshot->inventory_revision,
            .inventory_slots = from_replication_inventory_slots(snapshot->inventory),
            .inventory_max_stack_size = snapshot->inventory.max_stack_size,
            .rejection_reason = snapshot->response.rejection_reason,
        };
        static_cast<void>(
            gameplay_ui_->apply_machine_input_slot_transfer_confirmation(std::move(confirmation)));
        return {};
    }

    if (inventory_changed ||
        gameplay_ui_->inventory_authority_revision() != snapshot->inventory_revision) {
        if (!gameplay_ui_->apply_inventory_authoritative_snapshot(
                from_replication_inventory_slots(snapshot->inventory),
                snapshot->inventory.max_stack_size, snapshot->inventory_revision)) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "Client inventory UI rejected an authoritative replication snapshot"};
        }
    }
    return {};
}

void ScienceAndTheologyClientSession::process_lan_join_request() {
    if (!lan_server_browser_) return;
    std::optional<LanServerJoinRequest> request = lan_server_browser_->take_join_request();
    if (!request) return;

    auto connected = connect_tcp_udp(request->server.host, request->server.tcp_port,
                                     request->server.udp_port, std::move(request->server_password));
    if (!connected) {
        lan_server_browser_->report_connection_failure(
            "Unable to establish the TCP/UDP connection.");
        SNT_LOG_WARN("LAN server connection setup failed (host=%s tcp=%u udp=%u): %s",
                     request->server.host.c_str(), request->server.tcp_port, request->server.udp_port,
                     connected.error().format().c_str());
        set_lan_server_browser_visible(true);
        return;
    }
    set_lan_server_browser_visible(false);
}

snt::core::Expected<void> ScienceAndTheologyClientSession::create_client_world(
        snt::engine::ClientWorldSession& world_session) {
    if (!services_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Game session services are unavailable"};
    }

    auto& world = world_session.world();
    world_session.lighting().set_environment_lighting(
        make_environment_lighting(simulation_session_.day_night_state()));
    auto scene_result = snt::scene::load_scene(
        world, world_session.assets(), services_->paths().resolve_game(config_.scene.path));
    if (!scene_result) {
        auto error = scene_result.error();
        error.with_context("ScienceAndTheologyClientSession::create_client_world(load_scene)");
        return error;
    }

    const snt::ecs::EntityGuid camera_guid{config_.scene.active_camera_guid};
    const entt::entity camera_entity = world.find_entity_by_guid(camera_guid);
    if (camera_entity == entt::null ||
        !world.registry().all_of<snt::render::Transform, snt::render::Camera>(camera_entity)) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Configured active camera is absent from the game scene"};
    }

    // These cubes are only scene-generator verification content. The client
    // session owns their removal now that terrain is its primary presentation.
    for (const uint64_t guid : {2ull, 3ull}) {
        const entt::entity entity = world.find_entity_by_guid(snt::ecs::EntityGuid{guid});
        if (entity != entt::null) world.destroy_entity(entity);
    }

    auto& camera = world.registry().get<snt::render::Camera>(camera_entity);
    const auto& runtime_config = services_->config();
    camera.aspect = static_cast<float>(runtime_config.window.width) /
                    static_cast<float>(runtime_config.window.height);
    camera.fov = config_.camera.fov;
    camera.near_plane = config_.camera.near_plane;
    camera.far_plane = config_.camera.far_plane;
    if (auto result = world_session.set_active_camera(camera_guid); !result) {
        return result.error();
    }

    // Terrain generation belongs to the simulation session. Presentation only
    // observes its resulting generic chunk keys and schedules initial meshes.
    for (const auto& key : world_session.chunks().all_chunk_keys()) {
        world_session.chunk_render_system().mark_dirty(key);
    }

    auto& simulation = world_session.simulation();
    const bool has_authoritative_network_player = uses_network_presentation();
    presentation_world_ = &world;
    presentation_chunks_ = &world_session.chunks();
    chunk_render_system_ = &world_session.chunk_render_system();
    const GameEcosystemSystem* const ecosystem = simulation_session_.ecosystem_system();
    if (ecosystem == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Client simulation has no ecosystem system"};
    }
    auto creature_visuals = make_default_creature_presentation_visual_catalog(
        world_session.assets(), ecosystem->species_catalog());
    if (!creature_visuals) {
        auto error = creature_visuals.error();
        error.with_context(
            "ScienceAndTheologyClientSession::create_client_world(creature visuals)");
        return error;
    }
    creature_presentation_world_ = std::make_unique<GameCreaturePresentationWorld>(
        world, std::move(*creature_visuals));

    snt::player::PlayerControllerTuning tuning;
    tuning.move_speed = config_.camera.move_speed;
    tuning.look_speed = config_.camera.look_speed;
    if (!has_authoritative_network_player) {
        local_ecosystem_interest_provider_ = std::make_unique<ClientEcosystemInterestProvider>(
            world, camera_guid, config_.persistence.world_dimension_id);
        simulation_session_.set_ecosystem_interest_provider(
            local_ecosystem_interest_provider_.get());
        simulation_session_.set_creature_presentation_sink(creature_presentation_world_.get());
        auto player = std::make_shared<snt::player::PlayerControllerSystem>();
        player->set_input(&world_session.input());
        player->set_chunk_registry(&world_session.chunks());
        player->set_chunk_render_system(&world_session.chunk_render_system());
        player->set_camera_entity(camera_entity);
        player->set_dimension_id(config_.persistence.world_dimension_id);
        player->set_spawn_feet_position({config_.camera.initial_feet_position[0],
                                         config_.camera.initial_feet_position[1],
                                         config_.camera.initial_feet_position[2]});
        player->set_initial_look(-90.0f, -25.0f);
        player->set_tuning(tuning);
        auto player_physics = player->make_physics_system();
        if (auto result = simulation.register_main_system(player); !result) {
            auto error = result.error();
            error.with_context(
                "ScienceAndTheologyClientSession::create_client_world(register PlayerControllerSystem)");
            return error;
        }
        if (auto result = simulation.register_worker_system(std::move(player_physics)); !result) {
            auto error = result.error();
            error.with_context(
                "ScienceAndTheologyClientSession::create_client_world(register PlayerPhysicsSystem)");
            return error;
        }
        simulation.events().sink<snt::core::MouseLockChanged>()
            .connect<&snt::player::PlayerControllerSystem::on_mouse_lock_changed>(player.get());
    } else {
        simulation_session_.set_ecosystem_interest_provider(nullptr);
        simulation_session_.set_creature_presentation_sink(nullptr);
        remote_chunk_world_ = std::make_unique<replication::GameClientRemoteChunkWorld>(
            world_session.chunks());
        remote_machine_world_ = std::make_unique<replication::GameRemoteMachineWorld>();
        ensure_remote_replication_state();
        if (replication_session_) {
            SNT_LOG_INFO("Client movement uses server-authoritative input and position updates");
            SNT_LOG_INFO("Client terrain and machine presentation await authoritative replication");
        } else {
            SNT_LOG_INFO("Client presentation is waiting for a LAN server selection");
        }
    }

    // Match PlayerPhysicsSystem::sync_camera_transform before the first
    // fixed tick so the initial rendered frame uses the configured spawn.
    auto& camera_transform = world.registry().get<snt::render::Transform>(camera_entity);
    camera_transform.position[0] = config_.camera.initial_feet_position[0];
    camera_transform.position[1] = config_.camera.initial_feet_position[1] + tuning.eye_height;
    camera_transform.position[2] = config_.camera.initial_feet_position[2];
    camera_transform.rotation[0] = -25.0f;
    camera_transform.rotation[1] = -90.0f;
    camera_transform.rotation[2] = 0.0f;

    if (auto result = world_session.set_mouse_locked(true); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::create_client_world(mouse lock)");
        return error;
    }

    if (auto result = register_gameplay_ui_images(
            world_session.ui_images(), services_->paths(), &simulation_session_.content()); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::create_client_world(ui images)");
        return error;
    }

    InventoryState initial_inventory;
    if (uses_network_presentation()) {
        initial_inventory.slots.resize(config_.server_player.inventory_slots);
        initial_inventory.columns = 9;
        initial_inventory.max_stack_size = config_.server_player.inventory_max_stack_size;
    } else {
        initial_inventory = make_starting_inventory();
    }
    std::shared_ptr<IInventorySlotTransferCommandSink> slot_transfer_sink;
    std::shared_ptr<IMachineInputSlotTransferCommandSink> machine_input_slot_transfer_sink;
    if (!uses_network_presentation()) {
        local_inventory_authority_ = std::make_shared<LocalInventorySlotTransferAuthority>(
            initial_inventory);
        slot_transfer_sink = local_inventory_authority_;
        SNT_LOG_INFO("Inventory UI uses the offline authoritative slot-transaction simulator");
    } else {
        slot_transfer_sink = std::make_shared<ReplicationInventorySlotTransferCommandSink>(
            replication_session_, next_movement_sequence_);
        machine_input_slot_transfer_sink =
            std::make_shared<ReplicationMachineInputSlotTransferCommandSink>(
                replication_session_, next_movement_sequence_);
        SNT_LOG_INFO("Inventory UI uses authoritative full snapshots and changed-slot network deltas");
    }
    gameplay_ui_ = std::make_unique<GameplayUiController>(
        InventoryViewModel{std::move(initial_inventory)},
        make_starting_crafting_recipes(), std::move(slot_transfer_sink),
        std::move(machine_input_slot_transfer_sink), &simulation_session_.content(),
        &services_->paths());
    performance_ui_ = std::make_unique<PerformanceViewModel>();
    quest_book_ui_ = std::make_unique<QuestBookViewModel>(
        simulation_session_.content(), quest_book_state_.get(), this);
    static_cast<void>(quest_book_ui_->refresh());
    auto& layers = world_session.ui_layers();
    if (auto result = layers.register_screen({
            .owner_id = "science_and_theology",
            .screen_id = "gameplay",
            .layer = snt::ui::UiLayer::Screen,
            .initially_visible = true,
            .factory = make_gameplay_ui_factory(*gameplay_ui_, *localization_),
            .dispatch_action = [this](std::string_view action_id) {
                if (!gameplay_ui_) return;
                if (uses_network_presentation() && action_id.starts_with("craft:")) {
                    if (!network_crafting_unavailable_reported_) {
                        SNT_LOG_WARN("Network crafting is unavailable until it has an authoritative command path");
                        network_crafting_unavailable_reported_ = true;
                    }
                    return;
                }
                dispatch_gameplay_ui_action(*gameplay_ui_, action_id);
                if (local_inventory_authority_) {
                    auto synced = local_inventory_authority_->synchronize_offline_snapshot(
                        gameplay_ui_->inventory().state(),
                        gameplay_ui_->inventory_authority_revision());
                    if (!synced) {
                        SNT_LOG_ERROR("Offline inventory authority synchronization failed after UI action '%.*s': %s",
                                      static_cast<int>(action_id.size()), action_id.data(),
                                      synced.error().format().c_str());
                    }
                }
            },
        }); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::create_client_world(register gameplay UI)");
        return error;
    }
    if (auto result = layers.register_screen({
            .owner_id = "science_and_theology",
            .screen_id = "quest_book",
            .layer = snt::ui::UiLayer::Modal,
            .initially_visible = false,
            .factory = make_quest_book_ui_factory(*quest_book_ui_, [this] {
                set_quest_book_visible(false);
            }),
        }); !result) {
        const size_t removed = layers.unregister_owner("science_and_theology");
        SNT_LOG_WARN("Task-book UI registration failed; removed %zu partial UI screen(s)", removed);
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::create_client_world(register task book UI)");
        return error;
    }
    if (auto result = layers.register_screen({
            .owner_id = "science_and_theology",
            .screen_id = "performance",
            .layer = snt::ui::UiLayer::Hud,
            .initially_visible = performance_ui_->visible(),
            .factory = make_performance_ui_factory(*performance_ui_, *localization_),
        }); !result) {
        const size_t removed = layers.unregister_owner("science_and_theology");
        SNT_LOG_WARN("Performance UI registration failed; removed %zu partial UI screen(s)", removed);
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::create_client_world(register performance UI)");
        return error;
    }
    if (lan_server_browser_) {
        if (auto result = layers.register_screen({
                .owner_id = "science_and_theology",
                .screen_id = "lan_server_browser",
                .layer = snt::ui::UiLayer::Modal,
                .initially_visible = replication_session_ == nullptr,
                .factory = make_lan_server_browser_ui_factory(*lan_server_browser_, [this] {
                    set_lan_server_browser_visible(false);
                }),
            }); !result) {
            const size_t removed = layers.unregister_owner("science_and_theology");
            SNT_LOG_WARN("LAN browser UI registration failed; removed %zu partial UI screen(s)", removed);
            auto error = result.error();
            error.with_context(
                "ScienceAndTheologyClientSession::create_client_world(register LAN browser UI)");
            return error;
        }
    }
    ui_layers_ = &layers;
    expected_ui_screen_count_ = lan_server_browser_ ? 4u : 3u;
    if (local_player_identity_) {
        SNT_LOG_INFO("Client local player identity is '%s' (%s)",
                     local_player_identity_->account_id.c_str(),
                     player_identity_provider_name(local_player_identity_->provider));
    }
    SNT_LOG_INFO("ScienceAndTheology client world, gameplay UI, and task-book UI initialized");
    return {};
}

snt::core::Expected<void> ScienceAndTheologyClientSession::fixed_tick(
    snt::engine::FixedTickContext& context) {
    if (lan_server_browser_) {
        lan_server_browser_->fixed_tick(context.tick_index());
        process_lan_join_request();
    }
    if (replication_session_) {
        const snt::network::ReplicationTickContext replication_context{
            .tick_index = context.tick_index(),
            .delta_seconds = context.delta_seconds(),
        };
        if (auto result = replication_session_->poll_inbound(replication_context); !result) {
            auto error = result.error();
            error.with_context("ScienceAndTheologyClientSession::fixed_tick(replication inbound)");
            return error;
        }
        if (replication_session_->status().state ==
            replication::GameClientConnectionState::kDisconnected) {
            if (!replication_disconnect_reported_) {
                clear_remote_replication_state();
                replication_disconnect_reported_ = true;
                if (lan_server_browser_) {
                    lan_server_browser_->report_connection_failure(
                        "Server disconnected before or during authentication.");
                    set_lan_server_browser_visible(true);
                }
                SNT_LOG_WARN("Client replication session disconnected");
            }
        } else {
            replication_disconnect_reported_ = false;
            for (const replication::GameClientReplicationUpdate& update :
                 replication_session_->drain_replication_updates()) {
                const bool first_player_snapshot =
                    remote_player_world_ && remote_player_world_->active_snapshot_id() == 0;
                const bool first_quest_book_snapshot =
                    quest_book_state_ && quest_book_state_->active_snapshot_id() == 0;
                const bool first_inventory_snapshot =
                    remote_inventory_state_ && remote_inventory_state_->active_snapshot_id() == 0;
                const bool first_chunk_snapshot =
                    remote_chunk_world_ && remote_chunk_world_->active_snapshot_id() == 0;
                const bool first_machine_snapshot =
                    remote_machine_world_ && remote_machine_world_->active_snapshot_id() == 0;
                const replication::GameInventorySnapshot* const prior_inventory =
                    remote_inventory_state_ ? remote_inventory_state_->snapshot() : nullptr;
                const uint64_t prior_inventory_revision =
                    prior_inventory != nullptr ? prior_inventory->inventory_revision : 0;
                const uint64_t prior_inventory_response_revision =
                    prior_inventory != nullptr ? prior_inventory->response_revision : 0;
                if (remote_chunk_world_) {
                    auto applied = std::visit(
                        [this](const auto& value) { return remote_chunk_world_->apply(value); }, update);
                    if (!applied) {
                        auto error = applied.error();
                        error.with_context(
                            "ScienceAndTheologyClientSession::fixed_tick(remote chunk replication)");
                        return error;
                    }
                    if (chunk_render_system_) {
                        for (const snt::voxel::ChunkKey& key :
                             remote_chunk_world_->drain_dirty_chunks()) {
                            chunk_render_system_->mark_dirty(key);
                        }
                    }
                }
                if (remote_machine_world_) {
                    auto applied = std::visit(
                        [this](const auto& value) { return remote_machine_world_->apply(value); }, update);
                    if (!applied) {
                        auto error = applied.error();
                        error.with_context(
                            "ScienceAndTheologyClientSession::fixed_tick(remote machine replication)");
                        return error;
                    }
                }
                if (quest_book_state_) {
                    auto applied = std::visit(
                        [this](const auto& value) { return quest_book_state_->apply(value); }, update);
                    if (!applied) {
                        auto error = applied.error();
                        error.with_context(
                            "ScienceAndTheologyClientSession::fixed_tick(task-book replication)");
                        return error;
                    }
                }
                if (remote_inventory_state_) {
                    auto applied = std::visit(
                        [this](const auto& value) { return remote_inventory_state_->apply(value); },
                        update);
                    if (!applied) {
                        auto error = applied.error();
                        error.with_context(
                            "ScienceAndTheologyClientSession::fixed_tick(inventory replication)");
                        return error;
                    }
                    if (auto synchronized = apply_remote_inventory_to_ui(
                            prior_inventory_revision, prior_inventory_response_revision);
                        !synchronized) {
                        auto error = synchronized.error();
                        error.with_context(
                            "ScienceAndTheologyClientSession::fixed_tick(inventory UI synchronization)");
                        return error;
                    }
                }
                if (remote_player_world_) {
                    auto applied = std::visit(
                        [this](const auto& value) { return remote_player_world_->apply(value); }, update);
                    if (!applied) {
                        auto error = applied.error();
                        error.with_context(
                            "ScienceAndTheologyClientSession::fixed_tick(remote player replication)");
                        return error;
                    }
                }
                if (first_player_snapshot && remote_player_world_ &&
                    std::holds_alternative<replication::GameSnapshot>(update)) {
                    SNT_LOG_INFO("Applied authoritative player snapshot %llu with %zu player value(s)",
                                 static_cast<unsigned long long>(
                                     remote_player_world_->active_snapshot_id()),
                                 remote_player_world_->player_count());
                }
                if (first_quest_book_snapshot && quest_book_state_ &&
                    quest_book_state_->snapshot() != nullptr &&
                    std::holds_alternative<replication::GameSnapshot>(update)) {
                    SNT_LOG_INFO("Applied authoritative task-book snapshot %llu with %zu quest record(s)",
                                 static_cast<unsigned long long>(
                                     quest_book_state_->active_snapshot_id()),
                                 quest_book_state_->snapshot()->progress.size());
                }
                if (first_inventory_snapshot && remote_inventory_state_ &&
                    remote_inventory_state_->snapshot() != nullptr &&
                    std::holds_alternative<replication::GameSnapshot>(update)) {
                    SNT_LOG_INFO("Applied authoritative inventory snapshot %llu with %zu slot(s)",
                                 static_cast<unsigned long long>(
                                     remote_inventory_state_->active_snapshot_id()),
                                 remote_inventory_state_->snapshot()->inventory.slots.size());
                }
                if (first_chunk_snapshot && remote_chunk_world_ &&
                    std::holds_alternative<replication::GameSnapshot>(update)) {
                    SNT_LOG_INFO("Applied authoritative terrain snapshot %llu with %zu chunk(s)",
                                 static_cast<unsigned long long>(
                                     remote_chunk_world_->active_snapshot_id()),
                                 remote_chunk_world_->chunk_count());
                }
                if (first_machine_snapshot && remote_machine_world_ &&
                    std::holds_alternative<replication::GameSnapshot>(update)) {
                    SNT_LOG_INFO("Applied authoritative machine snapshot %llu with %zu machine value(s)",
                                 static_cast<unsigned long long>(
                                     remote_machine_world_->active_snapshot_id()),
                                 remote_machine_world_->machine_count());
                }
            }
            refresh_open_machine_panel();
            if (remote_player_world_) apply_authoritative_local_player();
        }
        if (replication_session_->status().state ==
            replication::GameClientConnectionState::kAuthenticated) {
            if (lan_server_browser_ && ui_layers_ &&
                ui_layers_->is_visible("science_and_theology", "lan_server_browser")) {
                lan_server_browser_->report_connection_established();
                set_lan_server_browser_visible(false);
            }
            const bool input_changed = !has_last_sent_movement_input_ ||
                !same_movement_intent(sampled_movement_input_, last_sent_movement_input_);
            const bool heartbeat_due = !has_last_sent_movement_input_ ||
                context.tick_index() - last_movement_send_tick_ >= kMovementInputHeartbeatTicks;
            if (input_changed || heartbeat_due) {
                auto input = sampled_movement_input_;
                input.client_sequence = next_movement_sequence_;
                if (auto result = replication_session_->enqueue_player_movement_input(input); !result) {
                    auto error = result.error();
                    error.with_context("ScienceAndTheologyClientSession::fixed_tick(player movement input)");
                    return error;
                }
                last_sent_movement_input_ = input;
                last_sent_movement_input_.client_sequence = 0;
                has_last_sent_movement_input_ = true;
                last_movement_send_tick_ = context.tick_index();
                ++next_movement_sequence_;
            }
        }
    }
    return simulation_session_.fixed_tick(context);
}

snt::core::Expected<void> ScienceAndTheologyClientSession::after_fixed_tick(
    snt::engine::FixedTickContext& context) {
    if (auto result = simulation_session_.after_fixed_tick(context); !result) return result.error();
    if (!replication_session_) return {};

    const snt::network::ReplicationTickContext replication_context{
        .tick_index = context.tick_index(),
        .delta_seconds = context.delta_seconds(),
    };
    if (auto result = replication_session_->emit_outbound(replication_context); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologyClientSession::after_fixed_tick(replication outbound)");
        return error;
    }
    return {};
}

void ScienceAndTheologyClientSession::frame(snt::engine::ClientFrameContext& context) {
    context.world().lighting().set_environment_lighting(
        make_environment_lighting(simulation_session_.day_night_state()));
    if (gameplay_ui_ && local_inventory_authority_) {
        for (InventorySlotTransferConfirmation confirmation :
             local_inventory_authority_->drain_slot_transfer_confirmations()) {
            static_cast<void>(
                gameplay_ui_->apply_inventory_slot_transfer_confirmation(std::move(confirmation)));
        }
    }
    handle_gameplay_input(context);
    sample_network_movement_input(context);

    if (config_.scripts.enabled && services_) {
        if (context.input().key_pressed[SDL_SCANCODE_F5]) {
            simulation_session_.request_content_reload(GameContentReloadTarget::kAll);
            SNT_LOG_INFO("Queued gameplay content reload target=all");
        }
    }

    if (performance_ui_) {
        const auto& stats = context.stats();
        performance_ui_->publish({
            .fps = stats.fps,
            .frame_ms = stats.frame_ms,
            .tps = stats.tps,
            .mspt = stats.mspt,
            .job_workers = stats.job_workers,
        });
    }
}

void ScienceAndTheologyClientSession::build_ui(snt::engine::ClientUiContext& context) {
    if (context.mouse_locked()) draw_crosshair(context);
}

void ScienceAndTheologyClientSession::shutdown() noexcept {
    if (ui_layers_) {
        const size_t removed = ui_layers_->unregister_owner("science_and_theology");
        if (removed != expected_ui_screen_count_) {
            SNT_LOG_WARN("Gameplay UI layer cleanup removed %zu screen(s), expected %zu", removed,
                         expected_ui_screen_count_);
        }
        ui_layers_ = nullptr;
    }
    expected_ui_screen_count_ = 0;
    simulation_session_.set_creature_presentation_sink(nullptr);
    simulation_session_.set_ecosystem_interest_provider(nullptr);
    local_ecosystem_interest_provider_.reset();
    if (creature_presentation_world_) creature_presentation_world_->clear();
    creature_presentation_world_.reset();
    presentation_world_ = nullptr;
    presentation_chunks_ = nullptr;
    clear_remote_replication_state();
    remote_chunk_world_.reset();
    remote_machine_world_.reset();
    chunk_render_system_ = nullptr;
    remote_player_world_.reset();
    remote_inventory_state_.reset();
    quest_book_ui_.reset();
    quest_book_state_.reset();
    if (replication_session_) replication_session_->shutdown();
    replication_session_.reset();
    lan_server_browser_.reset();
    connection_authentication_.reset();
    gameplay_ui_.reset();
    local_inventory_authority_.reset();
    performance_ui_.reset();
    localization_.reset();
    simulation_session_.shutdown();
    services_ = nullptr;
}

void ScienceAndTheologyClientSession::set_lan_server_browser_visible(bool visible) {
    if (!ui_layers_ || !lan_server_browser_) return;
    if (auto result = ui_layers_->set_visible(
            "science_and_theology", "lan_server_browser", visible); !result) {
        SNT_LOG_WARN("LAN browser UI layer visibility update failed: %s",
                     result.error().format().c_str());
        return;
    }
    SNT_LOG_INFO("LAN server browser %s", visible ? "opened" : "closed");
}

void ScienceAndTheologyClientSession::sample_network_movement_input(
    snt::engine::ClientFrameContext& context) {
    if (!replication_session_ || presentation_world_ == nullptr) return;
    const entt::entity camera_entity =
        presentation_world_->find_entity_by_guid(snt::ecs::EntityGuid{config_.scene.active_camera_guid});
    if (camera_entity == entt::null ||
        !presentation_world_->registry().all_of<snt::render::Transform>(camera_entity)) {
        return;
    }

    auto& transform = presentation_world_->registry().get<snt::render::Transform>(camera_entity);
    const auto& input = context.input();
    if (context.mouse_locked()) {
        transform.rotation[1] = normalize_yaw(
            transform.rotation[1] + input.mouse_dx * config_.camera.look_speed);
        transform.rotation[0] = std::clamp(
            transform.rotation[0] - input.mouse_dy * config_.camera.look_speed, -89.0f, 89.0f);
    }

    sampled_movement_input_ = {};
    if (!context.mouse_locked()) return;
    if (input.key_held[SDL_SCANCODE_W]) ++sampled_movement_input_.forward_axis;
    if (input.key_held[SDL_SCANCODE_S]) --sampled_movement_input_.forward_axis;
    if (input.key_held[SDL_SCANCODE_D]) ++sampled_movement_input_.strafe_axis;
    if (input.key_held[SDL_SCANCODE_A]) --sampled_movement_input_.strafe_axis;
    if (input.key_held[SDL_SCANCODE_LSHIFT]) {
        sampled_movement_input_.flags |= replication::kGamePlayerMovementFlagSprint;
    }
    if (input.key_pressed[SDL_SCANCODE_SPACE]) {
        sampled_movement_input_.flags |= replication::kGamePlayerMovementFlagJump;
    }
    const int yaw_centidegrees = std::clamp(
        static_cast<int>(std::lround(normalize_yaw(transform.rotation[1]) * 100.0f)),
        -18000, 17999);
    sampled_movement_input_.yaw_centidegrees = static_cast<int16_t>(yaw_centidegrees);
    sampled_movement_input_.pitch_centidegrees = static_cast<int16_t>(std::lround(
        std::clamp(transform.rotation[0], -89.0f, 89.0f) * 100.0f));
}

std::optional<GameClientBlockInteractionTarget>
ScienceAndTheologyClientSession::current_network_interaction_target() const {
    if (presentation_world_ == nullptr || presentation_chunks_ == nullptr ||
        !remote_player_world_) {
        return std::nullopt;
    }
    const auto local_player = remote_player_world_->authoritative_local_player();
    if (!local_player.has_value() || local_player->player.position.dimension_id.empty()) {
        return std::nullopt;
    }
    const entt::entity camera_entity = presentation_world_->find_entity_by_guid(
        snt::ecs::EntityGuid{config_.scene.active_camera_guid});
    if (camera_entity == entt::null ||
        !presentation_world_->registry().all_of<snt::render::Transform>(camera_entity)) {
        return std::nullopt;
    }
    const snt::render::Transform& transform =
        presentation_world_->registry().get<snt::render::Transform>(camera_entity);
    const std::string& dimension_id = local_player->player.position.dimension_id;
    const snt::player::CollisionWorldView collision_world(
        presentation_chunks_, dimension_id, false);
    const snt::player::RayCastResult hit = snt::player::ray_cast_voxels_dda(
        collision_world,
        {.x = transform.position[0], .y = transform.position[1], .z = transform.position[2]},
        camera_look_direction(transform), config_.client_interaction.raycast_reach_blocks);
    if (!hit.hit) return std::nullopt;

    const auto hit_cell = find_local_terrain_cell(
        *presentation_chunks_, dimension_id, hit.block.x, hit.block.y, hit.block.z);
    if (!hit_cell.has_value()) return std::nullopt;
    GameClientBlockInteractionTarget target{
        .dimension_id = dimension_id,
        .hit_x = hit.block.x,
        .hit_y = hit.block.y,
        .hit_z = hit.block.z,
        .hit_material = hit_cell->material,
    };
    if (hit.previous.x != hit.block.x || hit.previous.y != hit.block.y ||
        hit.previous.z != hit.block.z) {
        const auto placement_cell = find_local_terrain_cell(
            *presentation_chunks_, dimension_id, hit.previous.x, hit.previous.y, hit.previous.z);
        if (placement_cell.has_value()) {
            target.placement = GameClientBlockInteractionTarget::PlacementCell{
                .x = hit.previous.x,
                .y = hit.previous.y,
                .z = hit.previous.z,
                .expected_material = placement_cell->material,
            };
        }
    }
    return target;
}

bool ScienceAndTheologyClientSession::try_open_network_machine_panel() {
    if (!gameplay_ui_ || !remote_machine_world_ || !replication_session_ ||
        replication_session_->status().state !=
            replication::GameClientConnectionState::kAuthenticated) {
        return false;
    }
    const auto target = current_network_interaction_target();
    if (!target) return false;
    const auto machine = remote_machine_world_->find_machine_at(
        target->dimension_id, target->hit_x, target->hit_y, target->hit_z);
    if (!machine) return false;
    gameplay_ui_->open_machine(make_machine_panel_state(*machine, target->hit_material));
    if (!gameplay_ui_->machine_open()) return false;
    SNT_LOG_INFO("Machine UI opened for '%s' at %s (%d, %d, %d)",
                 machine->machine.machine_id.c_str(), target->dimension_id.c_str(),
                 target->hit_x, target->hit_y, target->hit_z);
    return true;
}

void ScienceAndTheologyClientSession::refresh_open_machine_panel() {
    if (!gameplay_ui_ || !gameplay_ui_->machine_open()) return;
    const MachinePanelState* const opened = gameplay_ui_->machine_panel().state();
    if (opened == nullptr || !remote_machine_world_) {
        gameplay_ui_->clear_machine_authority();
        return;
    }
    const auto machine = remote_machine_world_->find_machine_at(
        opened->dimension_id, opened->root_x, opened->root_y, opened->root_z);
    if (!machine) {
        gameplay_ui_->clear_machine_authority();
        return;
    }
    if (!gameplay_ui_->machine_panel().apply_authoritative_state(
            make_machine_panel_state(*machine, opened->expected_material))) {
        gameplay_ui_->clear_machine_authority();
    }
}

void ScienceAndTheologyClientSession::handle_network_block_interaction_input(
    snt::engine::ClientFrameContext& context) {
    if (!replication_session_ || presentation_world_ == nullptr || presentation_chunks_ == nullptr ||
        !remote_player_world_ || replication_session_->status().state !=
            replication::GameClientConnectionState::kAuthenticated) {
        return;
    }

    const auto& input = context.input();
    const GameClientBlockInteractionInput interaction_input{
        .mine_pressed = input.mouse_pressed[0],
        .context_pressed = input.mouse_pressed[2],
    };
    if (!interaction_input.mine_pressed && !interaction_input.context_pressed) return;

    const auto target_value = current_network_interaction_target();
    if (!target_value) return;
    GameClientBlockInteractionTarget target = *target_value;
    const std::string& dimension_id = target.dimension_id;

    std::string selected_item_id;
    if (gameplay_ui_) {
        const InventoryState& inventory = gameplay_ui_->inventory().state();
        const int32_t selected_index = gameplay_ui_->hotbar().selected_index();
        if (selected_index >= 0 &&
            selected_index < static_cast<int32_t>(inventory.slots.size()) &&
            !inventory.slots[selected_index].empty()) {
            selected_item_id = inventory.slots[selected_index].item_key;
        }
    }

    target.farming = make_farming_interaction_target(
        simulation_session_.content(), config_.client_interaction,
        simulation_session_.worldgen_config(), *presentation_chunks_, target, selected_item_id);

    std::optional<GameClientMachineInteractionTarget> machine_target;
    if (remote_machine_world_) {
        const auto remote_machine = remote_machine_world_->find_machine_at(
            dimension_id, target.hit_x, target.hit_y, target.hit_z);
        if (remote_machine.has_value()) {
            const MachineDefinition* definition = simulation_session_.content().find_machine(
                remote_machine->machine.machine_id);
            machine_target = GameClientMachineInteractionTarget{
                .machine_id = remote_machine->machine.machine_id,
                .has_collectible_output = has_collectible_machine_output(*remote_machine),
                .requires_manual_activation = definition != nullptr &&
                    definition->requires_manual_activation,
                .activation_hints = make_machine_activation_hints(
                    simulation_session_.content(), config_.client_interaction,
                    simulation_session_.worldgen_config(),
                    *presentation_chunks_, target, *remote_machine, selected_item_id),
            };
        }
    }

    ReplicationBlockInteractionCommandSink sink(*replication_session_, next_movement_sequence_);
    if (auto result = block_interaction_controller_.handle_input(
            interaction_input, target, std::move(selected_item_id), machine_target, sink);
        !result) {
        if (!block_interaction_submission_error_reported_) {
            SNT_LOG_WARN("Client block interaction command was not queued: %s",
                         result.error().format().c_str());
            block_interaction_submission_error_reported_ = true;
        }
        return;
    }
    block_interaction_submission_error_reported_ = false;
    if (!block_interaction_bindings_reported_) {
        SNT_LOG_INFO("Graphical client block interactions use local DDA targets and host-authoritative commits");
        block_interaction_bindings_reported_ = true;
    }
}

void ScienceAndTheologyClientSession::apply_authoritative_local_player() {
    if (presentation_world_ == nullptr || !remote_player_world_) return;
    const auto player = remote_player_world_->authoritative_local_player();
    if (!player.has_value()) return;
    const entt::entity camera_entity =
        presentation_world_->find_entity_by_guid(snt::ecs::EntityGuid{config_.scene.active_camera_guid});
    if (camera_entity == entt::null ||
        !presentation_world_->registry().all_of<snt::render::Transform>(camera_entity)) {
        return;
    }
    auto& transform = presentation_world_->registry().get<snt::render::Transform>(camera_entity);
    transform.position[0] = static_cast<float>(player->player.position.position.x);
    transform.position[1] = static_cast<float>(player->player.position.position.y) + 1.62f;
    transform.position[2] = static_cast<float>(player->player.position.position.z);
}

void ScienceAndTheologyClientSession::set_quest_book_visible(bool visible) {
    if (!ui_layers_ || !quest_book_ui_) return;
    if (visible) static_cast<void>(quest_book_ui_->refresh());
    if (auto result = ui_layers_->set_visible("science_and_theology", "quest_book", visible);
        !result) {
        SNT_LOG_WARN("Task-book UI layer visibility update failed: %s",
                     result.error().format().c_str());
        return;
    }
    SNT_LOG_INFO("Task-book UI %s", visible ? "opened" : "closed");
}

snt::core::Expected<void> ScienceAndTheologyClientSession::submit_quest_reward_claim(
    std::string_view quest_id) {
    if (quest_id.empty()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Task-book reward claim has an empty quest id"};
    }
    if (!replication_session_ || replication_session_->status().state !=
                                     replication::GameClientConnectionState::kAuthenticated) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Task-book reward claim requires an authenticated server session"};
    }
    if (next_movement_sequence_ == 0 ||
        next_movement_sequence_ == std::numeric_limits<uint64_t>::max()) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Client command sequence is exhausted"};
    }
    auto submitted = replication_session_->enqueue_quest_claim_reward(
        next_movement_sequence_, {.quest_id = std::string(quest_id)});
    if (!submitted) return submitted.error();
    SNT_LOG_INFO("Queued task-book reward claim sequence=%llu quest='%.*s'",
                 static_cast<unsigned long long>(next_movement_sequence_),
                 static_cast<int>(quest_id.size()), quest_id.data());
    ++next_movement_sequence_;
    return {};
}

void ScienceAndTheologyClientSession::handle_gameplay_input(snt::engine::ClientFrameContext& context) {
    if (!gameplay_ui_ && !quest_book_ui_ && !lan_server_browser_) return;
    const auto& input = context.input();
    const bool mouse_was_locked = context.mouse_locked();

    bool lan_browser_open = ui_layers_ &&
        ui_layers_->is_visible("science_and_theology", "lan_server_browser");
    if (lan_server_browser_ && input.key_pressed[SDL_SCANCODE_L]) {
        if (!lan_browser_open) lan_server_browser_->request_refresh();
        set_lan_server_browser_visible(!lan_browser_open);
        lan_browser_open = !lan_browser_open;
    }

    bool quest_book_open = ui_layers_ &&
        ui_layers_->is_visible("science_and_theology", "quest_book");
    if (!lan_browser_open && quest_book_ui_ && input.key_pressed[SDL_SCANCODE_J]) {
        if (!quest_book_open && gameplay_ui_) gameplay_ui_->close();
        set_quest_book_visible(!quest_book_open);
        quest_book_open = !quest_book_open;
    }
    if (!lan_browser_open && !quest_book_open && gameplay_ui_ && input.key_pressed[SDL_SCANCODE_E]) {
        if (gameplay_ui_->machine_open()) {
            gameplay_ui_->close();
            SNT_LOG_INFO("Machine UI closed");
        } else if (!uses_network_presentation() || !try_open_network_machine_panel()) {
            gameplay_ui_->toggle_inventory();
            SNT_LOG_INFO("Inventory UI %s", gameplay_ui_->inventory_open() ? "opened" : "closed");
        }
    }
    if (!lan_browser_open && !quest_book_open && gameplay_ui_ && input.key_pressed[SDL_SCANCODE_C]) {
        if (uses_network_presentation()) {
            if (!network_crafting_unavailable_reported_) {
                SNT_LOG_WARN("Network crafting UI is unavailable until it has an authoritative command path");
                network_crafting_unavailable_reported_ = true;
            }
        } else {
            gameplay_ui_->toggle_crafting();
            SNT_LOG_INFO("Crafting UI %s", gameplay_ui_->crafting_open() ? "opened" : "closed");
        }
    }
    if (input.key_pressed[SDL_SCANCODE_F3] && performance_ui_) {
        performance_ui_->toggle_visible();
        if (ui_layers_) {
            if (auto result = ui_layers_->set_visible(
                    "science_and_theology", "performance", performance_ui_->visible());
                !result) {
                SNT_LOG_WARN("Performance UI layer visibility update failed: %s",
                             result.error().format().c_str());
            }
        }
        SNT_LOG_INFO("Performance UI %s", performance_ui_->visible() ? "opened" : "closed");
    }

    bool gameplay_ui_open = gameplay_ui_ &&
        (gameplay_ui_->inventory_open() || gameplay_ui_->crafting_open() ||
         gameplay_ui_->machine_open());
    if (input.esc_pressed && lan_browser_open) {
        set_lan_server_browser_visible(false);
        lan_browser_open = false;
    } else if (input.esc_pressed && quest_book_open) {
        set_quest_book_visible(false);
        quest_book_open = false;
    } else if (input.esc_pressed && gameplay_ui_open) {
        gameplay_ui_->close();
        gameplay_ui_open = false;
        SNT_LOG_INFO("Gameplay UI closed");
    }

    if (!lan_browser_open && !quest_book_open && !gameplay_ui_open && gameplay_ui_) {
        for (int32_t index = 0; index < 9; ++index) {
            if (input.key_pressed[SDL_SCANCODE_1 + index]) {
                gameplay_ui_->hotbar().select(index);
                break;
            }
        }
        if (input.mouse_wheel_y > 0.0f) {
            gameplay_ui_->hotbar().select((gameplay_ui_->hotbar().selected_index() + 8) % 9);
        } else if (input.mouse_wheel_y < 0.0f) {
            gameplay_ui_->hotbar().select((gameplay_ui_->hotbar().selected_index() + 1) % 9);
        }
    }

    const bool ui_open = gameplay_ui_open || quest_book_open || lan_browser_open;
    if ((input.esc_pressed || ui_open) && context.mouse_locked()) {
        context.set_mouse_locked(false);
    } else if (input.wants_mouse_lock && !context.mouse_locked() && !ui_open) {
        context.set_mouse_locked(true);
    }
    if (mouse_was_locked && !ui_open) {
        handle_network_block_interaction_input(context);
    }
}

void ScienceAndTheologyClientSession::draw_crosshair(snt::engine::ClientUiContext& context) const {
    const float center_x = context.viewport_width() * 0.5f;
    const float center_y = context.viewport_height() * 0.5f;
    snt::ui::Arc2DCommandBuffer commands;
    append_cross_arm(commands, center_x, center_y, 1.0f, 1.0f, {0, 0, 0, 160});
    append_cross_arm(commands, center_x, center_y, 0.0f, 0.0f, {255, 255, 255, 220});
    context.submit(std::move(commands), snt::ui::UiLayer::Hud);
}

}  // namespace snt::game
