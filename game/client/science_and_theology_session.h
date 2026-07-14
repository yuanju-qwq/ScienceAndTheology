// ScienceAndTheology implementation of the game-session contract.

#pragma once

#include "engine/client_session.h"
#include "game_content_registry.h"
#include "gameplay_ui.h"
#include "game_session_config.h"
#include "game/world/game_chunk.h"

#include <memory>

namespace snt::game {

class ScienceAndTheologySession final : public snt::engine::IClientSession {
public:
    explicit ScienceAndTheologySession(GameSessionConfig config);
    ~ScienceAndTheologySession() override;

    snt::core::Expected<void> register_content(snt::engine::SimulationServices& services) override;
    snt::core::Expected<void> create_world(snt::engine::SimulationWorldSession& world) override;
    snt::core::Expected<void> create_client_world(snt::engine::ClientWorldSession& world) override;
    void fixed_tick(snt::engine::FixedTickContext& context) override;
    void frame(snt::engine::ClientFrameContext& context) override;
    void build_ui(snt::engine::ClientUiContext& context) override;
    void shutdown() noexcept override;

private:
    void handle_gameplay_input(snt::engine::ClientFrameContext& context);
    void draw_crosshair(snt::engine::ClientUiContext& context) const;

    GameSessionConfig config_;
    snt::engine::SimulationServices* services_ = nullptr;
    GameContentRegistry content_registry_;
    GameChunkSidecarRegistry chunk_sidecars_;
    std::unique_ptr<GameplayUiController> gameplay_ui_;
    std::unique_ptr<PerformanceViewModel> performance_ui_;
    bool scripts_started_ = false;
};

}  // namespace snt::game
