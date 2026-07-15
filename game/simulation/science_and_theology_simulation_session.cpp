// Game-owned deterministic simulation lifecycle.

#define SNT_LOG_CHANNEL "game.simulation_session"
#include "science_and_theology_simulation_session.h"

#include "game/client/demo_world_bootstrap.h"
#include "game/client/machine_tick_system.h"

#include "core/log.h"
#include "engine/simulation_services.h"
#include "script/script_manager.h"

#include <filesystem>
#include <memory>
#include <utility>

namespace snt::game {

ScienceAndTheologySimulationSession::ScienceAndTheologySimulationSession(GameSessionConfig config)
    : config_(std::move(config)), quest_registry_(content_registry_) {}

ScienceAndTheologySimulationSession::~ScienceAndTheologySimulationSession() { shutdown(); }

snt::core::Expected<void> ScienceAndTheologySimulationSession::register_content(
    snt::engine::SimulationServices& services) {
    services_ = &services;
    if (!config_.scripts.enabled) return {};

    auto& scripts = services.scripts();
    if (auto result = scripts.set_content_host(content_registry_); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySimulationSession::register_content(content host)");
        return error;
    }
    if (auto result = scripts.init(); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySimulationSession::register_content(ScriptManager)");
        return error;
    }
    scripts_started_ = true;

    const std::filesystem::path root(services.paths().resolve_game(config_.scripts.root));
    std::error_code error_code;
    if (!std::filesystem::is_directory(root, error_code) || error_code) {
        SNT_LOG_INFO("Gameplay script root not present; no modules loaded: %s", root.string().c_str());
        return {};
    }

    const auto result = config_.scripts.watch_for_changes
        ? scripts.watch_directory(root)
        : scripts.load_directory(root.string());
    if (!result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySimulationSession::register_content(gameplay scripts)");
        return error;
    }
    return {};
}

snt::core::Expected<void> ScienceAndTheologySimulationSession::create_world(
    snt::engine::SimulationWorldSession& world_session) {
    if (!services_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Game session services are unavailable"};
    }

    if (scripts_started_) {
        auto machine_system = std::make_shared<MachineTickSystem>(content_registry_);
        if (auto result = world_session.register_worker_system(std::move(machine_system)); !result) {
            auto error = result.error();
            error.with_context(
                "ScienceAndTheologySimulationSession::create_world(register MachineTickSystem)");
            return error;
        }
    }

    if (auto result = quest_registry_.refresh_definitions(); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySimulationSession::create_world(quest definitions)");
        return error;
    }

    if (auto result = bootstrap_demo_world(config_.demo, world_session.chunks(), chunk_sidecars_);
        !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySimulationSession::create_world(bootstrap_demo_world)");
        return error;
    }

    SNT_LOG_INFO("ScienceAndTheology simulation world initialized");
    return {};
}

snt::core::Expected<void> ScienceAndTheologySimulationSession::fixed_tick(
    snt::engine::FixedTickContext& context) {
    // File-watcher polling stays on the simulation main thread so a dedicated
    // server receives the same reload lifecycle as a graphical client.
    if (scripts_started_) {
        context.services().scripts().update(context.delta_seconds());
    }
    if (auto result = quest_registry_.tick(context.tick_index()); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySimulationSession::fixed_tick(quests)");
        return error;
    }
    return {};
}

snt::core::Expected<void> ScienceAndTheologySimulationSession::after_fixed_tick(
    snt::engine::FixedTickContext&) {
    return {};
}

void ScienceAndTheologySimulationSession::shutdown() noexcept {
    quest_registry_.clear();
    if (scripts_started_ && services_) {
        services_->scripts().shutdown();
        scripts_started_ = false;
    }
    services_ = nullptr;
}

}  // namespace snt::game
