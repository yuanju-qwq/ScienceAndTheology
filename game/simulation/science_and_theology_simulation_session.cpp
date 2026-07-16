// Game-owned deterministic simulation lifecycle.

#define SNT_LOG_CHANNEL "game.simulation_session"
#include "science_and_theology_simulation_session.h"

#include "game/client/demo_world_bootstrap.h"
#include "game/client/machine_tick_system.h"
#include "game/simulation/machine_runtime_persistence.h"
#include "game/world/save/world_persistence_lifecycle.h"

#include "core/log.h"
#include "engine/simulation_services.h"
#include "script/script_manager.h"

#include <filesystem>
#include <memory>
#include <utility>

namespace snt::game {

ScienceAndTheologySimulationSession::ScienceAndTheologySimulationSession(GameSessionConfig config)
    : config_(std::move(config)),
      quest_registry_(content_registry_),
      machine_interactions_(content_registry_) {}

ScienceAndTheologySimulationSession::~ScienceAndTheologySimulationSession() { shutdown(); }

void ScienceAndTheologySimulationSession::set_machine_tick_event_sink(
    IMachineTickEventSink* event_sink) noexcept {
    machine_tick_event_sink_ = event_sink;
    if (machine_tick_system_) machine_tick_system_->set_event_sink(event_sink);
}

snt::core::Expected<void> ScienceAndTheologySimulationSession::register_content(
    snt::engine::SimulationServices& services) {
    services_ = &services;
    if (config_.persistence.world_save_enabled) {
        world_persistence_ = std::make_unique<GameWorldPersistenceLifecycle>(
            GameWorldPersistenceDescriptor{
                .universe_save_dir = services.paths().resolve_user(
                    config_.persistence.universe_save_dir),
                .dimension_id = config_.persistence.world_dimension_id,
                .seed = static_cast<int64_t>(config_.demo.seed),
                .universe_mode = config_.persistence.universe_mode,
            });
    }
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
        machine_tick_system_ = std::make_shared<MachineTickSystem>(
            content_registry_, machine_tick_event_sink_);
        if (auto result = world_session.register_worker_system(machine_tick_system_); !result) {
            machine_tick_system_.reset();
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

    world_ = &world_session.world();
    chunks_ = &world_session.chunks();
    world_ready_ = false;
    bool loaded_existing_world = false;
    if (world_persistence_) {
        auto loaded = world_persistence_->load_existing(*chunks_, chunk_sidecars_);
        if (!loaded) {
            auto error = loaded.error();
            error.with_context("ScienceAndTheologySimulationSession::create_world(load world)");
            return error;
        }
        loaded_existing_world = *loaded;
    }

    if (!loaded_existing_world) {
        if (auto result = bootstrap_demo_world(config_.demo, *chunks_, chunk_sidecars_); !result) {
            auto error = result.error();
            error.with_context("ScienceAndTheologySimulationSession::create_world(bootstrap_demo_world)");
            return error;
        }
    } else {
        SNT_LOG_INFO("Game world loaded from current-format persistence; demo bootstrap skipped");
    }
    if (auto result = GameMachineRuntimePersistence::restore(*world_, chunk_sidecars_); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySimulationSession::create_world(restore machines)");
        return error;
    }
    world_ready_ = true;

    SNT_LOG_INFO("ScienceAndTheology simulation world initialized");
    return {};
}

snt::core::Expected<void> ScienceAndTheologySimulationSession::fixed_tick(
    snt::engine::FixedTickContext& context) {
    if (machine_tick_system_) machine_tick_system_->set_tick_index(context.tick_index());
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
    if (machine_tick_system_) machine_tick_system_->set_event_sink(nullptr);
    machine_tick_system_.reset();
    if (world_persistence_ && world_ready_ && world_ != nullptr && chunks_ != nullptr) {
        if (auto result = GameMachineRuntimePersistence::capture(*world_, chunk_sidecars_); !result) {
            SNT_LOG_ERROR("Game machine runtime capture during session shutdown failed: %s",
                          result.error().format().c_str());
        } else if (auto result = world_persistence_->save(*chunks_, chunk_sidecars_); !result) {
            SNT_LOG_ERROR("Game world save during session shutdown failed: %s",
                          result.error().format().c_str());
        }
    }
    world_ready_ = false;
    world_ = nullptr;
    chunks_ = nullptr;
    world_persistence_.reset();
    quest_registry_.clear();
    if (scripts_started_ && services_) {
        services_->scripts().shutdown();
        scripts_started_ = false;
    }
    services_ = nullptr;
}

}  // namespace snt::game
