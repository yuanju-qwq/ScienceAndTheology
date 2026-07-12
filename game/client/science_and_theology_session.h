// ScienceAndTheology implementation of the game-session contract.

#pragma once

#include "engine/game_session.h"
#include "game_content_registry.h"
#include "gameplay_ui.h"
#include "game_session_config.h"

#include <memory>

namespace snt::game {

class ScienceAndTheologySession final : public snt::engine::IGameSession {
public:
    explicit ScienceAndTheologySession(GameSessionConfig config);
    ~ScienceAndTheologySession() override;

    snt::core::Expected<void> register_content(snt::engine::RuntimeServices& services) override;
    snt::core::Expected<void> create_world(snt::engine::WorldSession& world) override;
    void fixed_tick(snt::engine::FixedTickContext& context) override;
    void frame(snt::engine::FrameContext& context) override;
    void build_ui(snt::engine::UiContext& context) override;
    void shutdown() noexcept override;

private:
    void handle_gameplay_input(snt::engine::FrameContext& context);
    void draw_crosshair(snt::engine::UiContext& context) const;

    GameSessionConfig config_;
    snt::engine::RuntimeServices* services_ = nullptr;
    GameContentRegistry content_registry_;
    std::unique_ptr<GameplayUiController> gameplay_ui_;
    std::unique_ptr<PerformanceViewModel> performance_ui_;
    bool scripts_started_ = false;
};

}  // namespace snt::game
