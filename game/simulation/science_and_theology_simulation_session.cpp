// Game-owned deterministic simulation lifecycle.

#define SNT_LOG_CHANNEL "game.simulation_session"
#include "science_and_theology_simulation_session.h"

#include "game/client/demo_world_bootstrap.h"
#include "game/client/machine_tick_system.h"
#include "game/simulation/block_physics_system.h"
#include "game/simulation/crop_growth_system.h"
#include "game/simulation/ecosystem_system.h"
#include "game/simulation/wild_creature_system.h"
#include "game/simulation/game_fluid_system.h"
#include "game/simulation/machine_runtime_persistence.h"
#include "game/simulation/offline_industrial_network_island.h"
#include "game/simulation/offline_machine_simulation.h"
#include "game/simulation/tree_growth_system.h"
#include "game/simulation/worldgen_script_content.h"
#include "game/world/save/world_persistence_lifecycle.h"

#include "core/log.h"
#include "engine/simulation_services.h"
#include "script/script_manager.h"

#include <filesystem>
#include <memory>
#include <utility>

namespace snt::game {

class ScienceAndTheologySimulationSession::SessionEcosystemEnvironmentProvider final
    : public IGameEcosystemEnvironmentProvider {
public:
    explicit SessionEcosystemEnvironmentProvider(
        const ScienceAndTheologySimulationSession& session) noexcept
        : session_(&session) {}

    bool sample_ecosystem_environment(
        const ChunkKey& chunk, GameEcosystemEnvironmentSample& out_sample) const override {
        if (session_ == nullptr) return false;
        out_sample.enabled = session_->config_.gameplay.is_ecosystem_enabled(chunk.dimension_id);
        out_sample.rate_multiplier = session_->config_.gameplay.get_ecosystem_rate_multiplier(
            chunk.dimension_id);
        out_sample.is_daytime = session_->day_night_state().is_daytime;
        return true;
    }

private:
    const ScienceAndTheologySimulationSession* session_ = nullptr;
};

ScienceAndTheologySimulationSession::ScienceAndTheologySimulationSession(GameSessionConfig config)
    : config_(std::move(config)),
      quest_registry_(content_registry_),
      machine_interactions_(content_registry_) {
    session_ecosystem_environment_provider_ =
        std::make_unique<SessionEcosystemEnvironmentProvider>(*this);
    day_night_cycle_.update(0, 0.05f, config_.gameplay,
                            config_.persistence.world_dimension_id);
    season_cycle_.update(0, 0.05f, config_.gameplay,
                         config_.persistence.world_dimension_id);
}

ScienceAndTheologySimulationSession::~ScienceAndTheologySimulationSession() { shutdown(); }

void ScienceAndTheologySimulationSession::request_content_reload(
    GameContentReloadTarget target) noexcept {
    pending_content_reload_ = target;
}

std::vector<GameContentReloadTargetInfo>
ScienceAndTheologySimulationSession::content_reload_targets() const {
    return content_reload_service_.targets();
}

void ScienceAndTheologySimulationSession::set_machine_tick_event_sink(
    IMachineTickEventSink* event_sink) noexcept {
    machine_tick_event_sink_ = event_sink;
    if (machine_tick_system_) machine_tick_system_->set_event_sink(event_sink);
    if (offline_machine_simulation_) offline_machine_simulation_->set_event_sink(event_sink);
}

void ScienceAndTheologySimulationSession::set_block_physics_mutation_sink(
    IBlockPhysicsMutationSink* mutation_sink) noexcept {
    block_physics_mutation_sink_ = mutation_sink;
    if (block_physics_system_) block_physics_system_->set_mutation_sink(mutation_sink);
}

void ScienceAndTheologySimulationSession::set_fluid_mutation_sink(
    IFluidMutationSink* mutation_sink) noexcept {
    fluid_mutation_sink_ = mutation_sink;
    if (fluid_system_) fluid_system_->set_mutation_sink(mutation_sink);
}

void ScienceAndTheologySimulationSession::set_fluid_presentation_sink(
    IFluidPresentationSink* presentation_sink) noexcept {
    fluid_presentation_sink_ = presentation_sink;
    if (fluid_system_) fluid_system_->set_presentation_sink(presentation_sink);
}

void ScienceAndTheologySimulationSession::set_fluid_telemetry_sink(
    IFluidSimulationTelemetrySink* telemetry_sink) noexcept {
    fluid_telemetry_sink_ = telemetry_sink;
    if (fluid_system_) fluid_system_->set_telemetry_sink(telemetry_sink);
}

void ScienceAndTheologySimulationSession::set_fluid_compute_backend(
    IFluidComputeBackend* backend) noexcept {
    fluid_compute_backend_ = backend;
    if (fluid_system_) fluid_system_->set_compute_backend(backend);
}

void ScienceAndTheologySimulationSession::set_tree_growth_mutation_sink(
    ITreeGrowthMutationSink* mutation_sink) noexcept {
    tree_growth_mutation_sink_ = mutation_sink;
    if (tree_growth_system_) tree_growth_system_->set_mutation_sink(mutation_sink);
}

void ScienceAndTheologySimulationSession::set_crop_growth_mutation_sink(
    ICropGrowthMutationSink* mutation_sink) noexcept {
    crop_growth_mutation_sink_ = mutation_sink;
    if (crop_growth_system_) crop_growth_system_->set_mutation_sink(mutation_sink);
}

void ScienceAndTheologySimulationSession::set_ecosystem_environment_provider(
    const IGameEcosystemEnvironmentProvider* environment_provider) noexcept {
    ecosystem_environment_provider_ = environment_provider;
    if (ecosystem_system_) {
        ecosystem_system_->set_environment_provider(
            environment_provider != nullptr ? environment_provider
                                            : session_ecosystem_environment_provider_.get());
    }
}

void ScienceAndTheologySimulationSession::set_ecosystem_interest_provider(
    const IGameEcosystemInterestProvider* interest_provider) noexcept {
    ecosystem_interest_provider_ = interest_provider;
    if (ecosystem_system_) ecosystem_system_->set_interest_provider(interest_provider);
}

void ScienceAndTheologySimulationSession::set_ecosystem_mutation_sink(
    IGameEcosystemMutationSink* mutation_sink) noexcept {
    ecosystem_mutation_sink_ = mutation_sink;
    if (ecosystem_system_) ecosystem_system_->set_mutation_sink(mutation_sink);
}

void ScienceAndTheologySimulationSession::set_creature_presentation_sink(
    IGameCreaturePresentationSink* sink) noexcept {
    creature_presentation_sink_ = sink;
    if (wild_creature_system_) wild_creature_system_->set_presentation_sink(sink);
}

void ScienceAndTheologySimulationSession::set_region_topology_event_sink(
    IRegionTopologyEventSink* event_sink) noexcept {
    region_topology_.set_event_sink(event_sink);
}

void ScienceAndTheologySimulationSession::schedule_block_physics_after_terrain_mutation(
    std::string_view dimension_id, int32_t block_x, int32_t block_y,
    int32_t block_z, uint64_t source_tick) {
    if (block_physics_system_) {
        block_physics_system_->schedule_after_terrain_mutation(
            dimension_id, block_x, block_y, block_z, source_tick);
    }
}

void ScienceAndTheologySimulationSession::schedule_fluid_after_terrain_mutation(
    std::string_view dimension_id, int32_t block_x, int32_t block_y,
    int32_t block_z, uint64_t source_tick) {
    if (fluid_system_) {
        fluid_system_->schedule_after_terrain_mutation(
            dimension_id, block_x, block_y, block_z, source_tick);
    }
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
    gameplay_content_loaded_ = false;

    const std::filesystem::path root(services.paths().resolve_game(config_.scripts.root));
    if (auto result = content_reload_service_.configure(root); !result) {
        auto error = result.error();
        error.with_context("ScienceAndTheologySimulationSession::register_content(reload service)");
        return error;
    }
    scripts.set_file_change_handler(&content_reload_service_);
    std::error_code error_code;
    if (!std::filesystem::is_directory(root, error_code) || error_code) {
        return snt::core::Error{
            snt::core::ErrorCode::kFileNotFound,
            "World-generation script root is unavailable: " + root.string()};
    }

    if (config_.scripts.enabled) {
        const auto result = config_.scripts.watch_for_changes
            ? scripts.watch_directory(root)
            : scripts.load_directory(root.string());
        if (!result) {
            auto error = result.error();
            error.with_context(
                "ScienceAndTheologySimulationSession::register_content(gameplay scripts)");
            return error;
        }
        gameplay_content_loaded_ = true;
        return {};
    }

    const std::filesystem::path worldgen_catalog = root / "50_worldgen_catalog.as";
    if (auto result = scripts.load_file(worldgen_catalog.string()); !result) {
        auto error = result.error();
        error.with_context(
            "ScienceAndTheologySimulationSession::register_content(world-generation script)");
        return error;
    }
    SNT_LOG_INFO("Loaded mandatory world-generation script while gameplay scripts are disabled");
    return {};
}

snt::core::Expected<void> ScienceAndTheologySimulationSession::create_world(
    snt::engine::SimulationWorldSession& world_session) {
    if (!services_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Game session services are unavailable"};
    }
    if (auto result = content_registry_.validate_machine_placement_references(); !result) {
        auto error = result.error();
        error.with_context(
            "ScienceAndTheologySimulationSession::create_world(machine placements)");
        return error;
    }

    if (gameplay_content_loaded_) {
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
    if (!scripts_started_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "World-generation script runtime is unavailable"};
    }
    auto worldgen_config = build_worldgen_config_from_script(services_->scripts());
    if (!worldgen_config) {
        auto error = worldgen_config.error();
        error.with_context("ScienceAndTheologySimulationSession::create_world(worldgen script)");
        return error;
    }
    worldgen_config_ = std::move(*worldgen_config);
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
        if (auto result = bootstrap_demo_world(config_.demo, *chunks_, chunk_sidecars_,
                                               worldgen_config_);
            !result) {
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
    offline_industrial_network_provider_ =
        std::make_unique<OfflineIndustrialNetworkIslandProvider>(
        content_registry_, chunk_sidecars_);
    offline_industrial_network_simulator_ =
        std::make_unique<OfflineIndustrialNetworkIslandSimulator>();
    offline_machine_simulation_ = std::make_unique<OfflineMachineSimulationService>(
        content_registry_, chunk_sidecars_);
    offline_machine_simulation_->set_event_sink(machine_tick_event_sink_);
    offline_machine_simulation_->set_network_island_provider(
        offline_industrial_network_provider_.get());
    offline_machine_simulation_->set_network_island_simulator(
        offline_industrial_network_simulator_.get());
    if (auto result = offline_machine_simulation_->initialize(0); !result) {
        offline_machine_simulation_.reset();
        auto error = result.error();
        error.with_context("ScienceAndTheologySimulationSession::create_world(offline machines)");
        return error;
    }
    block_physics_system_ = std::make_unique<GameBlockPhysicsSystem>(
        *chunks_, *worldgen_config_, config_.gameplay);
    block_physics_system_->set_mutation_sink(block_physics_mutation_sink_);
    fluid_system_ = std::make_unique<GameFluidSystem>(*chunks_, *worldgen_config_);
    fluid_system_->set_mutation_sink(fluid_mutation_sink_);
    fluid_system_->set_presentation_sink(fluid_presentation_sink_);
    fluid_system_->set_telemetry_sink(fluid_telemetry_sink_);
    fluid_system_->set_compute_backend(fluid_compute_backend_);
    fluid_system_->initialize_loaded_chunks();
    tree_growth_system_ = std::make_unique<GameTreeGrowthSystem>(
        *chunks_, chunk_sidecars_, *worldgen_config_);
    tree_growth_system_->set_mutation_sink(tree_growth_mutation_sink_);
    crop_growth_system_ = std::make_unique<GameCropGrowthSystem>(
        *chunks_, chunk_sidecars_, *worldgen_config_);
    crop_growth_system_->set_mutation_sink(crop_growth_mutation_sink_);
    ecosystem_system_ = std::make_unique<GameEcosystemSystem>(
        *chunks_, chunk_sidecars_, *worldgen_config_);
    ecosystem_system_->set_environment_provider(
        ecosystem_environment_provider_ != nullptr ? ecosystem_environment_provider_
                                                   : session_ecosystem_environment_provider_.get());
    ecosystem_system_->set_interest_provider(ecosystem_interest_provider_);
    ecosystem_system_->set_mutation_sink(ecosystem_mutation_sink_);
    wild_creature_system_ = std::make_unique<GameWildCreatureSystem>(
        *ecosystem_system_, *chunks_, chunk_sidecars_, ecosystem_system_->species_catalog());
    wild_creature_system_->set_presentation_sink(creature_presentation_sink_);
    ecosystem_system_->set_wild_proxy_sink(wild_creature_system_.get());
    ecosystem_system_->set_far_visual_sink(wild_creature_system_.get());
    ecosystem_system_->set_captive_lifecycle_sink(wild_creature_system_.get());
    world_ready_ = true;

    SNT_LOG_INFO("Game block physics initialized with the current world-generation snapshot");
    SNT_LOG_INFO("Game hybrid fluid simulation initialized with sparse, dense, and equilibrium layers");
    SNT_LOG_INFO("Game tree growth initialized with typed chunk-sidecar state");
    SNT_LOG_INFO("Game crop growth initialized with typed chunk-sidecar state");
    SNT_LOG_INFO("Game ecosystem initialized with typed chunk-sidecar population state");
    SNT_LOG_INFO("Game region topology initialized for authoritative fixed ticks");
    SNT_LOG_INFO("ScienceAndTheology simulation world initialized");
    return {};
}

snt::core::Expected<void> ScienceAndTheologySimulationSession::fixed_tick(
    snt::engine::FixedTickContext& context) {
    day_night_cycle_.update(context.tick_index(), context.delta_seconds(), config_.gameplay,
                            config_.persistence.world_dimension_id);
    season_cycle_.update(context.tick_index(), context.delta_seconds(), config_.gameplay,
                         config_.persistence.world_dimension_id);
    (void)region_topology_.fixed_tick(context.tick_index());
    if (offline_machine_simulation_) {
        if (auto result = offline_machine_simulation_->tick(context.tick_index()); !result) {
            auto error = result.error();
            error.with_context("ScienceAndTheologySimulationSession::fixed_tick(offline machines)");
            return error;
        }
    }
    if (ecosystem_system_) {
        ecosystem_system_->tick(context.tick_index(), season_cycle_.state().season);
    }
    if (block_physics_system_) block_physics_system_->tick(context.tick_index());
    if (fluid_system_) fluid_system_->tick(context.tick_index());
    if (tree_growth_system_) tree_growth_system_->tick(context.tick_index());
    if (crop_growth_system_) {
        crop_growth_system_->tick(context.tick_index(), season_cycle_.state().season);
    }
    if (machine_tick_system_) machine_tick_system_->set_tick_index(context.tick_index());
    // File-watcher polling stays on the simulation main thread so a dedicated
    // server receives the same reload lifecycle as a graphical client.
    if (scripts_started_) {
        if (pending_content_reload_) {
            const GameContentReloadTarget target = *pending_content_reload_;
            pending_content_reload_.reset();
            auto reloaded = content_reload_service_.reload(context.services().scripts(), target);
            if (!reloaded) {
                // A malformed editor change must not stop the deterministic
                // session. ScriptManager has already restored the previously
                // committed batch; retain a value-only failure for the page.
                last_content_reload_result_.reset();
                last_content_reload_failure_ = GameContentReloadFailure{
                    target, reloaded.error().format()};
            } else {
                last_content_reload_failure_.reset();
                last_content_reload_result_ = std::move(*reloaded);
            }
        }
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

snt::core::Expected<OfflineChunkMachineTransition>
ScienceAndTheologySimulationSession::dematerialize_chunk_machines(
    const ChunkKey& chunk_key, uint64_t current_tick) {
    if (world_ == nullptr || offline_machine_simulation_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Offline machine simulation is unavailable"};
    }
    return offline_machine_simulation_->dematerialize_chunk(*world_, chunk_key, current_tick);
}

snt::core::Expected<OfflineChunkMachineTransition>
ScienceAndTheologySimulationSession::dematerialize_chunks_machines(
    std::span<const ChunkKey> chunk_keys,
    uint64_t current_tick) {
    if (world_ == nullptr || offline_machine_simulation_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Offline machine simulation is unavailable"};
    }
    return offline_machine_simulation_->dematerialize_chunks(*world_, chunk_keys, current_tick);
}

snt::core::Expected<void> ScienceAndTheologySimulationSession::materialize_chunk_machines(
    const ChunkKey& chunk_key, uint64_t current_tick) {
    if (world_ == nullptr || offline_machine_simulation_ == nullptr) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Offline machine simulation is unavailable"};
    }
    return offline_machine_simulation_->materialize_chunk(*world_, chunk_key, current_tick);
}

void ScienceAndTheologySimulationSession::shutdown() noexcept {
    region_topology_.set_event_sink(nullptr);
    region_topology_.clear();
    if (ecosystem_system_) {
        ecosystem_system_->set_environment_provider(nullptr);
        ecosystem_system_->set_interest_provider(nullptr);
        ecosystem_system_->set_mutation_sink(nullptr);
        ecosystem_system_->set_wild_proxy_sink(nullptr);
        ecosystem_system_->set_far_visual_sink(nullptr);
        ecosystem_system_->set_captive_lifecycle_sink(nullptr);
    }
    if (wild_creature_system_) wild_creature_system_->set_presentation_sink(nullptr);
    wild_creature_system_.reset();
    ecosystem_system_.reset();
    ecosystem_environment_provider_ = nullptr;
    ecosystem_interest_provider_ = nullptr;
    ecosystem_mutation_sink_ = nullptr;
    creature_presentation_sink_ = nullptr;
    if (crop_growth_system_) crop_growth_system_->set_mutation_sink(nullptr);
    crop_growth_system_.reset();
    crop_growth_mutation_sink_ = nullptr;
    if (tree_growth_system_) tree_growth_system_->set_mutation_sink(nullptr);
    tree_growth_system_.reset();
    tree_growth_mutation_sink_ = nullptr;
    if (block_physics_system_) block_physics_system_->set_mutation_sink(nullptr);
    block_physics_system_.reset();
    block_physics_mutation_sink_ = nullptr;
    if (fluid_system_) {
        fluid_system_->set_mutation_sink(nullptr);
        fluid_system_->set_presentation_sink(nullptr);
        fluid_system_->set_telemetry_sink(nullptr);
        fluid_system_->set_compute_backend(nullptr);
    }
    fluid_system_.reset();
    fluid_mutation_sink_ = nullptr;
    fluid_presentation_sink_ = nullptr;
    fluid_telemetry_sink_ = nullptr;
    fluid_compute_backend_ = nullptr;
    if (machine_tick_system_) machine_tick_system_->set_event_sink(nullptr);
    machine_tick_system_.reset();
    if (offline_machine_simulation_) {
        if (auto result = offline_machine_simulation_->flush(
                offline_machine_simulation_->last_tick()); !result) {
            SNT_LOG_ERROR("Offline machine flush during session shutdown failed: %s",
                          result.error().format().c_str());
        }
        offline_machine_simulation_->set_event_sink(nullptr);
        offline_machine_simulation_->set_network_island_provider(nullptr);
        offline_machine_simulation_->set_network_island_simulator(nullptr);
        offline_machine_simulation_.reset();
    }
    offline_industrial_network_simulator_.reset();
    offline_industrial_network_provider_.reset();
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
    worldgen_config_.reset();
    world_persistence_.reset();
    quest_registry_.clear();
    if (scripts_started_ && services_) {
        services_->scripts().set_file_change_handler(nullptr);
        services_->scripts().shutdown();
        scripts_started_ = false;
    }
    gameplay_content_loaded_ = false;
    pending_content_reload_.reset();
    last_content_reload_result_.reset();
    last_content_reload_failure_.reset();
    content_reload_service_.reset();
    services_ = nullptr;
}

}  // namespace snt::game
