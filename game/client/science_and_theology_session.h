// ScienceAndTheology graphical session.
//
// This is a ClientRuntime-only presentation adapter. Deterministic game
// content lives in ScienceAndTheologySimulationSession so the dedicated
// server and graphical client run the same simulation lifecycle.

#pragma once

#include "client_block_interaction.h"
#include "creature_interaction.h"
#include "ground_loot_interaction.h"
#include "engine/client_session.h"
#include "game/network/game_chunk_replication.h"
#include "game/network/game_client_replication_session.h"
#include "game/localization/localization.h"
#include "gameplay_ui.h"
#include "lan_server_browser.h"
#include "quest_book_ui.h"
#include "game_session_config.h"
#include "game/simulation/science_and_theology_simulation_session.h"

#include <memory>
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>

namespace snt::game::replication {
class GameRemoteAeNetworkWorld;
class GameRemoteAutomationControllerWorld;
class GameClientInventoryState;
class GameClientQuestBookState;
class GameRemoteCreatureWorld;
class GameRemoteGroundLootWorld;
class GameRemotePlayerWorld;
}

namespace snt::ecs {
class World;
}

namespace snt::voxel {
class ChunkRegistry;
class ChunkRenderSystem;
}

namespace snt::game {

class GameCreaturePresentationWorld;
class GameGroundLootPresentationWorld;
class IGameEcosystemInterestProvider;

class ScienceAndTheologyClientSession final : public snt::engine::IClientSession,
                                              public IQuestBookCommandSink {
public:
    explicit ScienceAndTheologyClientSession(
        GameSessionConfig config,
        std::shared_ptr<localization::LocalizationService> localization,
        std::optional<replication::GameClientAuthentication> connection_authentication = std::nullopt);
    ~ScienceAndTheologyClientSession() override;

    snt::core::Expected<void> register_content(snt::engine::SimulationServices& services) override;
    snt::core::Expected<void> create_world(snt::engine::SimulationWorldSession& world) override;
    snt::core::Expected<void> create_client_world(snt::engine::ClientWorldSession& world) override;
    snt::core::Expected<void> fixed_tick(snt::engine::FixedTickContext& context) override;
    snt::core::Expected<void> after_fixed_tick(snt::engine::FixedTickContext& context) override;
    void frame(snt::engine::ClientFrameContext& context) override;
    void build_ui(snt::engine::ClientUiContext& context) override;
    void shutdown() noexcept override;

    [[nodiscard]] const PlayerIdentity* local_player_identity() const noexcept {
        return local_player_identity_ ? &*local_player_identity_ : nullptr;
    }
    [[nodiscard]] const replication::GameClientQuestBookState* quest_book_state() const noexcept {
        return quest_book_state_.get();
    }
    [[nodiscard]] const replication::GameRemoteGroundLootWorld* remote_ground_loot_world() const noexcept {
        return remote_ground_loot_world_.get();
    }
    void request_content_reload(GameContentReloadTarget target) noexcept {
        simulation_session_.request_content_reload(target);
    }
    [[nodiscard]] std::vector<GameContentReloadTargetInfo> content_reload_targets() const {
        return simulation_session_.content_reload_targets();
    }
    [[nodiscard]] const GameContentReloadResult* last_content_reload_result() const noexcept {
        return simulation_session_.last_content_reload_result();
    }
    [[nodiscard]] const GameContentReloadFailure* last_content_reload_failure() const noexcept {
        return simulation_session_.last_content_reload_failure();
    }

private:
    [[nodiscard]] bool uses_network_presentation() const noexcept;
    [[nodiscard]] snt::core::Expected<void> connect_tcp_udp(std::string host,
                                                             uint16_t tcp_port,
                                                             uint16_t udp_port,
                                                             std::string server_password);
    void ensure_remote_replication_state();
    void clear_remote_replication_state();
    [[nodiscard]] snt::core::Expected<void> apply_remote_inventory_to_ui(
        uint64_t previous_inventory_revision, uint64_t previous_response_revision);
    void process_lan_join_request();
    void set_lan_server_browser_visible(bool visible);
    void handle_gameplay_input(snt::engine::ClientFrameContext& context);
    [[nodiscard]] std::optional<GameClientBlockInteractionTarget>
    current_network_interaction_target() const;
    [[nodiscard]] std::optional<GameClientCreatureInteractionTarget>
    current_network_creature_interaction_target() const;
    [[nodiscard]] std::optional<GameClientGroundLootInteractionTarget>
    current_network_ground_loot_interaction_target() const;
    [[nodiscard]] bool try_open_network_ae_network_panel();
    [[nodiscard]] bool try_open_network_automation_controller_panel();
    [[nodiscard]] bool try_open_network_machine_panel();
    void refresh_open_automation_controller_panel();
    void refresh_open_ae_network_panel();
    void refresh_open_machine_panel();
    [[nodiscard]] bool handle_network_creature_interaction_input(
        snt::engine::ClientFrameContext& context);
    [[nodiscard]] bool handle_network_ground_loot_interaction_input(
        snt::engine::ClientFrameContext& context);
    void handle_network_block_interaction_input(snt::engine::ClientFrameContext& context);
    void set_quest_book_visible(bool visible);
    [[nodiscard]] snt::core::Expected<void> submit_quest_reward_claim(
        std::string_view quest_id) override;
    void sample_network_movement_input(snt::engine::ClientFrameContext& context);
    void apply_authoritative_local_player();
    void draw_crosshair(snt::engine::ClientUiContext& context) const;

    GameSessionConfig config_;
    GameClientBlockInteractionController block_interaction_controller_;
    GameClientCreatureInteractionController creature_interaction_controller_;
    GameClientGroundLootInteractionController ground_loot_interaction_controller_;
    std::shared_ptr<localization::LocalizationService> localization_;
    ScienceAndTheologySimulationSession simulation_session_;
    std::optional<PlayerIdentity> local_player_identity_;
    std::optional<replication::GameClientAuthentication> connection_authentication_;
    std::unique_ptr<replication::GameClientReplicationSession> replication_session_;
    std::unique_ptr<LanServerBrowserModel> lan_server_browser_;
    std::unique_ptr<replication::GameClientRemoteChunkWorld> remote_chunk_world_;
    std::unique_ptr<replication::GameRemoteMachineWorld> remote_machine_world_;
    std::unique_ptr<replication::GameRemoteAutomationControllerWorld>
        remote_automation_controller_world_;
    std::unique_ptr<replication::GameRemoteAeNetworkWorld> remote_ae_network_world_;
    std::unique_ptr<replication::GameRemoteCreatureWorld> remote_creature_world_;
    std::unique_ptr<replication::GameRemoteGroundLootWorld> remote_ground_loot_world_;
    std::unique_ptr<replication::GameRemotePlayerWorld> remote_player_world_;
    std::unique_ptr<replication::GameClientInventoryState> remote_inventory_state_;
    std::unique_ptr<replication::GameClientQuestBookState> quest_book_state_;
    std::unique_ptr<QuestBookViewModel> quest_book_ui_;
    std::unique_ptr<GameCreaturePresentationWorld> creature_presentation_world_;
    std::unique_ptr<GameGroundLootPresentationWorld> ground_loot_presentation_world_;
    std::unique_ptr<IGameEcosystemInterestProvider> local_ecosystem_interest_provider_;
    snt::ecs::World* presentation_world_ = nullptr;
    snt::voxel::ChunkRegistry* presentation_chunks_ = nullptr;
    snt::voxel::ChunkRenderSystem* chunk_render_system_ = nullptr;
    replication::GamePlayerMovementInput sampled_movement_input_;
    replication::GamePlayerMovementInput last_sent_movement_input_;
    uint64_t next_outbound_sequence_ = 1;
    uint64_t last_movement_send_tick_ = 0;
    bool has_last_sent_movement_input_ = false;
    snt::engine::SimulationServices* services_ = nullptr;
    snt::ui::UiLayerStack* ui_layers_ = nullptr;
    size_t expected_ui_screen_count_ = 0;
    bool replication_disconnect_reported_ = false;
    bool block_interaction_bindings_reported_ = false;
    bool block_interaction_submission_error_reported_ = false;
    bool creature_interaction_bindings_reported_ = false;
    bool creature_interaction_submission_error_reported_ = false;
    bool ground_loot_interaction_bindings_reported_ = false;
    bool ground_loot_interaction_submission_error_reported_ = false;
    bool network_crafting_unavailable_reported_ = false;
    std::shared_ptr<LocalInventorySlotTransferAuthority> local_inventory_authority_;
    std::unique_ptr<GameplayUiController> gameplay_ui_;
    std::unique_ptr<PerformanceViewModel> performance_ui_;
};

}  // namespace snt::game
