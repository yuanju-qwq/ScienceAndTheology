// ScienceAndTheology graphical session.
//
// This is a ClientRuntime-only presentation adapter. Deterministic game
// content lives in ScienceAndTheologySimulationSession so the dedicated
// server and graphical client run the same simulation lifecycle.

#pragma once

#include "engine/client_session.h"
#include "gameplay_ui.h"
#include "game_session_config.h"
#include "game/player/player_identity.h"
#include "game/simulation/science_and_theology_simulation_session.h"

#include <memory>
#include <optional>

namespace snt::game {

class ScienceAndTheologyClientSession final : public snt::engine::IClientSession {
public:
    explicit ScienceAndTheologyClientSession(GameSessionConfig config);
    ScienceAndTheologyClientSession(GameSessionConfig config, PlayerIdentity local_player_identity);
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
    void draw_crosshair(snt::engine::ClientUiContext& context) const;

    GameSessionConfig config_;
    ScienceAndTheologySimulationSession simulation_session_;
    std::optional<PlayerIdentity> local_player_identity_;
    snt::engine::SimulationServices* services_ = nullptr;
    std::unique_ptr<GameplayUiController> gameplay_ui_;
    std::unique_ptr<PerformanceViewModel> performance_ui_;
};

}  // namespace snt::game
