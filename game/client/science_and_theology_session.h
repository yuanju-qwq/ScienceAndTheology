// ScienceAndTheology graphical session.
//
// This is a ClientRuntime-only presentation adapter. Deterministic game
// content lives in ScienceAndTheologySimulationSession so the dedicated
// server and graphical client run the same simulation lifecycle.

#pragma once

#include "engine/client_session.h"
#include "game/network/game_client_replication_session.h"
#include "game/localization/localization.h"
#include "gameplay_ui.h"
#include "game_session_config.h"
#include "game/simulation/science_and_theology_simulation_session.h"

#include <memory>
#include <optional>
#include <cstdint>

namespace snt::game::replication {
class GameRemotePlayerWorld;
}

namespace snt::ecs {
class World;
}

namespace snt::game {

class ScienceAndTheologyClientSession final : public snt::engine::IClientSession {
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

private:
    void handle_gameplay_input(snt::engine::ClientFrameContext& context);
    void sample_network_movement_input(snt::engine::ClientFrameContext& context);
    void apply_authoritative_local_player();
    void draw_crosshair(snt::engine::ClientUiContext& context) const;

    GameSessionConfig config_;
    std::shared_ptr<localization::LocalizationService> localization_;
    ScienceAndTheologySimulationSession simulation_session_;
    std::optional<PlayerIdentity> local_player_identity_;
    std::optional<replication::GameClientAuthentication> connection_authentication_;
    std::unique_ptr<replication::GameClientReplicationSession> replication_session_;
    std::unique_ptr<replication::GameRemotePlayerWorld> remote_player_world_;
    snt::ecs::World* presentation_world_ = nullptr;
    replication::GamePlayerMovementInput sampled_movement_input_;
    replication::GamePlayerMovementInput last_sent_movement_input_;
    uint64_t next_movement_sequence_ = 1;
    uint64_t last_movement_send_tick_ = 0;
    bool has_last_sent_movement_input_ = false;
    snt::engine::SimulationServices* services_ = nullptr;
    std::unique_ptr<GameplayUiController> gameplay_ui_;
    std::unique_ptr<PerformanceViewModel> performance_ui_;
};

}  // namespace snt::game
