#include "gd_tick_system.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/resource.hpp>

#include "core/simulation/event_types.hpp"
#include "core/simulation/tree_growth_system.hpp"
#include "core/simulation/crop_growth_system.hpp"
#include "core/simulation/season_system.hpp"
#include "core/simulation/region_system.hpp"
#include "core/simulation/region_graph.hpp"
#include "core/simulation/ecosystem_system.hpp"
#include "bindings/world/gd_world_data.h"

namespace science_and_theology {

GDTickSystem::GDTickSystem() {
    tick_system_ = std::make_unique<TickSystem>(nullptr);
}

GDTickSystem::~GDTickSystem() {
    unsubscribe_from_event_bus();
}

void GDTickSystem::_ready() {
}

void GDTickSystem::_process(double delta) {
    // Manual tick — GDScript should call tick() explicitly.
    // If auto_tick is desired, uncomment:
    // if (world_set) tick(static_cast<float>(delta));
}

void GDTickSystem::set_world_data(godot::Resource* gd_world) {
    unsubscribe_from_event_bus();
    gd_world_data_ = gd_world;
    WorldData* world_ptr = get_world_data_ptr();
    if (world_ptr) {
        tick_system_ = std::make_unique<TickSystem>(world_ptr);
        world_set = true;
        subscribe_to_event_bus();
    }
}

void GDTickSystem::register_tree_growth_system() {
    if (tick_system_) {
        tick_system_->register_subsystem(std::make_unique<TreeGrowthSystem>());
    }
}

void GDTickSystem::register_crop_growth_system() {
    if (tick_system_) {
        tick_system_->register_subsystem(std::make_unique<CropGrowthSystem>());
    }
}

void GDTickSystem::register_season_system() {
    if (tick_system_) {
        tick_system_->register_subsystem(std::make_unique<SeasonSystem>());
    }
}

void GDTickSystem::register_day_night_system() {
    if (tick_system_) {
        auto sys = std::make_unique<DayNightSystem>();
        day_night_system_ = sys.get();
        tick_system_->register_subsystem(std::move(sys));
    }
}

void GDTickSystem::register_region_system() {
    if (tick_system_) {
        auto sys = std::make_unique<RegionSystem>();
        region_system_ = sys.get();
        tick_system_->register_subsystem(std::move(sys));
    }
}

void GDTickSystem::register_ecosystem_system() {
    if (tick_system_) {
        auto sys = std::make_unique<EcosystemSystem>();
        ecosystem_system_ = sys.get();
        tick_system_->register_subsystem(std::move(sys));
    }
}

godot::Dictionary GDTickSystem::get_day_night_state() const {
    godot::Dictionary d;
    if (!day_night_system_) {
        // Return defaults when the system is not registered.
        d["time_of_day"] = 0.5f;
        d["sun_elevation"] = 1.5708f;
        d["sun_azimuth"] = 0.0f;
        d["sun_light_energy"] = 2.2f;
        d["sun_color_r"] = 1.0f;
        d["sun_color_g"] = 1.0f;
        d["sun_color_b"] = 1.0f;
        d["ambient_energy"] = 0.62f;
        d["ambient_color_r"] = 0.5f;
        d["ambient_color_g"] = 0.5f;
        d["ambient_color_b"] = 0.5f;
        d["is_daytime"] = true;
        d["moon_elevation"] = -1.5708f;
        d["moon_azimuth"] = 3.14159f;
        d["moon_light_energy"] = 0.0f;
        d["moon_color_r"] = 0.6f;
        d["moon_color_g"] = 0.65f;
        d["moon_color_b"] = 0.8f;
        return d;
    }
    const auto& s = day_night_system_->current_state();
    d["time_of_day"] = s.time_of_day;
    d["sun_elevation"] = s.sun_elevation;
    d["sun_azimuth"] = s.sun_azimuth;
    d["sun_light_energy"] = s.sun_light_energy;
    d["sun_color_r"] = s.sun_color_r;
    d["sun_color_g"] = s.sun_color_g;
    d["sun_color_b"] = s.sun_color_b;
    d["ambient_energy"] = s.ambient_energy;
    d["ambient_color_r"] = s.ambient_color_r;
    d["ambient_color_g"] = s.ambient_color_g;
    d["ambient_color_b"] = s.ambient_color_b;
    d["is_daytime"] = s.is_daytime;
    d["moon_elevation"] = s.moon_elevation;
    d["moon_azimuth"] = s.moon_azimuth;
    d["moon_light_energy"] = s.moon_light_energy;
    d["moon_color_r"] = s.moon_color_r;
    d["moon_color_g"] = s.moon_color_g;
    d["moon_color_b"] = s.moon_color_b;
    return d;
}

float GDTickSystem::get_time_of_day() const {
    if (!day_night_system_) return 0.5f;
    return day_night_system_->time_of_day();
}

bool GDTickSystem::get_is_daytime() const {
    if (!day_night_system_) return true;
    return day_night_system_->is_daytime();
}

int64_t GDTickSystem::get_region_count() const {
    if (!region_system_) return 0;
    return static_cast<int64_t>(region_system_->total_region_count());
}

int64_t GDTickSystem::get_region_count_by_type(int64_t type_index) const {
    if (!region_system_) return 0;
    if (type_index < 0 || type_index >= static_cast<int64_t>(RegionType::COUNT)) {
        return 0;
    }
    return static_cast<int64_t>(
        region_system_->region_count(static_cast<RegionType>(type_index)));
}

godot::Dictionary GDTickSystem::get_region_data(
    int64_t type_index, int64_t region_id) const {
    godot::Dictionary d;
    if (!region_system_) return d;
    if (type_index < 0 || type_index >= static_cast<int64_t>(RegionType::COUNT)) {
        return d;
    }

    auto type = static_cast<RegionType>(type_index);
    const auto* graph = region_system_->get_graph(type);
    if (!graph) return d;

    const auto* data = graph->get_region_data(static_cast<uint64_t>(region_id));
    if (!data) return d;

    d["region_id"] = static_cast<int64_t>(data->region_id);
    d["type"] = static_cast<int64_t>(data->type);
    d["dimension"] = godot::String(data->dimension_id.c_str());
    d["pollution"] = data->pollution;
    d["temperature"] = data->temperature;
    d["node_count"] = static_cast<int64_t>(data->node_count);
    return d;
}

// --- Ecosystem query ---

godot::Dictionary GDTickSystem::get_population_data(
    const godot::String& dimension, int cx, int cy, int cz) const {
    godot::Dictionary d;
    if (!ecosystem_system_) {
        d["vegetation"] = 0.0f;
        d["herbivore"] = 0.0f;
        d["predator"] = 0.0f;
        d["fertility"] = 0.0f;
        d["water"] = 0.0f;
        d["dead_biomass"] = 0.0f;
        return d;
    }

    ChunkKey key(dimension.utf8().get_data(), cx, cy, cz);
    const PopulationCell* cell = ecosystem_system_->get_population_cell(key);
    if (!cell) {
        d["vegetation"] = 0.0f;
        d["herbivore"] = 0.0f;
        d["predator"] = 0.0f;
        d["fertility"] = 0.0f;
        d["water"] = 0.0f;
        d["dead_biomass"] = 0.0f;
        return d;
    }

    d["vegetation"] = cell->vegetation_density;
    d["herbivore"] = cell->herbivore_density;
    d["predator"] = cell->predator_density;
    d["fertility"] = cell->soil_fertility;
    d["water"] = cell->water_availability;
    d["dead_biomass"] = cell->dead_biomass;
    return d;
}

float GDTickSystem::get_total_vegetation() const {
    if (!ecosystem_system_) return 0.0f;
    return ecosystem_system_->total_vegetation();
}

float GDTickSystem::get_total_herbivore() const {
    if (!ecosystem_system_) return 0.0f;
    return ecosystem_system_->total_herbivore();
}

float GDTickSystem::get_total_predator() const {
    if (!ecosystem_system_) return 0.0f;
    return ecosystem_system_->total_predator();
}

int64_t GDTickSystem::get_total_proxy_count() const {
    if (!ecosystem_system_) return 0;
    return static_cast<int64_t>(ecosystem_system_->total_proxy_count());
}

godot::Array GDTickSystem::get_proxy_data(
    const godot::String& dimension, int cx, int cy, int cz) const {
    godot::Array result;
    if (!ecosystem_system_ || !world_set) return result;

    ChunkKey key(dimension.utf8().get_data(), cx, cy, cz);
    const EcosystemSystem::ProxyGroup* group =
        ecosystem_system_->get_proxy_group(key);
    if (!group) return result;

    auto& registry = get_world_data_ptr()->block_entity_registry();

    auto add_creature = [&](EntityId id) {
        const CreatureBlockEntityState* cs = registry.get_creature_state(id);
        if (!cs) return;

        godot::Dictionary d;
        d["id"] = static_cast<int64_t>(id.id);

        // Resolve species name from registry.
        const CreatureSpeciesDef* def =
            ecosystem_system_->species_registry().get_species(cs->species_id);
        if (def) {
            d["species"] = godot::String(def->species_key.c_str());
            d["title_key"] = godot::String(def->title_key.c_str());
            d["model_key"] = godot::String(def->model_key.c_str());
            d["model_scale"] = def->model_scale;
        } else {
            d["species"] = godot::String("unknown");
            d["title_key"] = godot::String("Unknown");
            d["model_key"] = godot::String("");
            d["model_scale"] = 1.0f;
        }
        d["role"] = godot::String(
            kCreatureRoleNames[static_cast<int>(cs->creature_role)]);

        const char* state_name = "Idle";
        switch (cs->ai_state) {
            case CreatureState::WANDERING: state_name = "Wandering"; break;
            case CreatureState::FLEEING:   state_name = "Fleeing"; break;
            default: break;
        }
        d["state"] = godot::String(state_name);
        d["pos_x"] = cs->pos_x;
        d["pos_y"] = cs->pos_y;
        d["pos_z"] = cs->pos_z;
        d["health"] = cs->health;
        result.append(d);
    };

    for (EntityId id : group->herbivore_ids) {
        add_creature(id);
    }
    for (EntityId id : group->predator_ids) {
        add_creature(id);
    }

    return result;
}

void GDTickSystem::sync_ecosystem_to_chunks() {
    if (!ecosystem_system_) return;
    ecosystem_system_->sync_all_populations_to_chunks();
}

void GDTickSystem::restore_ecosystem_from_chunks() {
    if (!ecosystem_system_) return;
    ecosystem_system_->restore_populations_from_chunks();
}

godot::Dictionary GDTickSystem::attack_creature(
    const godot::String& dimension,
    const godot::Vector3& player_pos,
    const godot::Vector3& look_dir,
    float reach, float damage) {
    godot::Dictionary result;
    result["hit"] = false;
    result["killed"] = false;
    result["creature_id"] = static_cast<int64_t>(0);
    result["species_id"] = static_cast<int64_t>(0);
    result["damage_dealt"] = 0.0f;
    result["remaining_health"] = 0.0f;

    godot::Dictionary chunk_dict;
    chunk_dict["dimension"] = dimension;
    chunk_dict["cx"] = 0;
    chunk_dict["cy"] = 0;
    chunk_dict["cz"] = 0;
    result["chunk"] = chunk_dict;

    if (!ecosystem_system_ || !tick_system_) return result;

    int64_t tick = tick_system_->tick_count();
    auto ar = ecosystem_system_->attack_creature(
        dimension.utf8().get_data(),
        player_pos.x, player_pos.y, player_pos.z,
        look_dir.x, look_dir.y, look_dir.z,
        reach, damage, tick);

    result["hit"] = ar.hit;
    result["killed"] = ar.killed;
    result["creature_id"] = static_cast<int64_t>(ar.creature_id);
    result["species_id"] = static_cast<int64_t>(ar.species_id);
    result["damage_dealt"] = ar.damage_dealt;
    result["remaining_health"] = ar.remaining_health;

    chunk_dict["dimension"] = godot::String(ar.chunk_key.dimension_id.c_str());
    chunk_dict["cx"] = ar.chunk_key.chunk_x;
    chunk_dict["cy"] = ar.chunk_key.chunk_y;
    chunk_dict["cz"] = ar.chunk_key.chunk_z;
    result["chunk"] = chunk_dict;

    return result;
}

godot::Array GDTickSystem::get_species_drops(int64_t species_id) const {
    godot::Array result;
    if (!ecosystem_system_) return result;

    const CreatureSpeciesDef* def =
        ecosystem_system_->species_registry().get_species(
            static_cast<uint16_t>(species_id));
    if (!def) return result;

    for (const auto& drop : def->drops) {
        godot::Dictionary d;
        d["item_key"] = godot::String(drop.item_key.c_str());
        d["chance"] = drop.chance;
        d["min_count"] = drop.min_count;
        d["max_count"] = drop.max_count;
        result.append(d);
    }
    return result;
}

bool GDTickSystem::feed_creatures(
    const godot::String& dimension,
    int64_t cx, int64_t cy, int64_t cz,
    int64_t role, float amount) {
    if (!ecosystem_system_) return false;

    ChunkKey key(dimension.utf8().get_data(),
                 static_cast<int>(cx),
                 static_cast<int>(cy),
                 static_cast<int>(cz));

    CreatureRole creature_role = (role == static_cast<int64_t>(CreatureRole::HERBIVORE))
        ? CreatureRole::HERBIVORE : CreatureRole::PREDATOR;

    return ecosystem_system_->feed_creatures(key, creature_role, amount);
}

godot::Dictionary GDTickSystem::feed_creature_at(
    const godot::String& dimension,
    const godot::Vector3& player_pos,
    const godot::Vector3& look_dir,
    float reach) {
    godot::Dictionary result;
    result["hit"] = false;
    result["creature_id"] = static_cast<int64_t>(0);
    result["species_id"] = static_cast<int64_t>(0);
    result["outcome"] = godot::String("miss");

    godot::Dictionary chunk_dict;
    chunk_dict["dimension"] = dimension;
    chunk_dict["cx"] = 0;
    chunk_dict["cy"] = 0;
    chunk_dict["cz"] = 0;
    result["chunk"] = chunk_dict;

    if (!ecosystem_system_) return result;

    auto fr = ecosystem_system_->feed_creature_at(
        dimension.utf8().get_data(),
        player_pos.x, player_pos.y, player_pos.z,
        look_dir.x, look_dir.y, look_dir.z,
        reach);

    result["hit"] = fr.hit;
    result["creature_id"] = static_cast<int64_t>(fr.creature_id);
    result["species_id"] = static_cast<int64_t>(fr.species_id);
    result["outcome"] = godot::String(fr.outcome.c_str());

    chunk_dict["dimension"] = godot::String(fr.chunk_key.dimension_id.c_str());
    chunk_dict["cx"] = fr.chunk_key.chunk_x;
    chunk_dict["cy"] = fr.chunk_key.chunk_y;
    chunk_dict["cz"] = fr.chunk_key.chunk_z;
    result["chunk"] = chunk_dict;

    return result;
}

int64_t GDTickSystem::get_total_captive_count() const {
    if (!ecosystem_system_) return 0;
    return static_cast<int64_t>(ecosystem_system_->total_captive_count());
}

godot::Array GDTickSystem::get_captive_data(
    const godot::String& dimension, int cx, int cy, int cz) const {
    godot::Array result;
    if (!ecosystem_system_) return result;

    ChunkKey key(dimension.utf8().get_data(), cx, cy, cz);
    auto data = ecosystem_system_->get_captive_data(key);

    for (const auto& info : data) {
        godot::Dictionary d;
        d["runtime_id"] = static_cast<int64_t>(info.runtime_id);
        d["species_id"] = static_cast<int64_t>(info.species_id);
        d["age_stage"] = static_cast<int64_t>(info.age_stage);
        d["is_tamed"] = info.is_tamed;
        d["is_pregnant"] = info.is_pregnant;
        d["pos_x"] = info.pos_x;
        d["pos_y"] = info.pos_y;
        d["pos_z"] = info.pos_z;
        result.append(d);
    }
    return result;
}

void GDTickSystem::tick(float delta) {
    if (!tick_system_ || !world_set) return;
    tick_system_->tick(delta);
    const std::string profiler_log = tick_system_->consume_profiler_log();
    if (!profiler_log.empty()) {
        godot::UtilityFunctions::print(godot::String(profiler_log.c_str()));
    }
}

void GDTickSystem::add_player_chunk(
    int64_t player_handle, const godot::String& dimension,
    int cx, int cy, int cz) {
    if (tick_system_ && world_set) {
        tick_system_->add_player_chunk(
            static_cast<PlayerHandle>(player_handle),
            dimension.utf8().get_data(), cx, cy, cz);
    }
}

void GDTickSystem::remove_player_chunk(int64_t player_handle) {
    if (tick_system_ && world_set) {
        tick_system_->remove_player_chunk(static_cast<PlayerHandle>(player_handle));
    }
}

void GDTickSystem::clear_player_chunks() {
    if (tick_system_ && world_set) {
        tick_system_->clear_player_chunks();
    }
}

int64_t GDTickSystem::get_player_count() const {
    return tick_system_ ? static_cast<int64_t>(tick_system_->player_count()) : 0;
}

godot::String GDTickSystem::get_player_dimension(int64_t player_handle) const {
    if (!tick_system_) return godot::String();
    return godot::String(tick_system_->get_player_dimension(
        static_cast<PlayerHandle>(player_handle)).c_str());
}

int64_t GDTickSystem::get_active_radius() const {
    return tick_system_ ? tick_system_->active_radius() : 0;
}

void GDTickSystem::set_active_radius(int64_t radius) {
    if (tick_system_) {
        tick_system_->set_active_radius(static_cast<int>(radius));
    }
}

int64_t GDTickSystem::get_sleep_near_interval() const {
    return tick_system_ ? tick_system_->sleep_near_interval() : 0;
}

void GDTickSystem::set_sleep_near_interval(int64_t interval) {
    if (tick_system_) {
        tick_system_->set_sleep_near_interval(static_cast<int>(interval));
    }
}

int64_t GDTickSystem::get_sleep_mid_interval() const {
    return tick_system_ ? tick_system_->sleep_mid_interval() : 0;
}

void GDTickSystem::set_sleep_mid_interval(int64_t interval) {
    if (tick_system_) {
        tick_system_->set_sleep_mid_interval(static_cast<int>(interval));
    }
}

int64_t GDTickSystem::get_sleep_far_interval() const {
    return tick_system_ ? tick_system_->sleep_far_interval() : 0;
}

void GDTickSystem::set_sleep_far_interval(int64_t interval) {
    if (tick_system_) {
        tick_system_->set_sleep_far_interval(static_cast<int>(interval));
    }
}

void GDTickSystem::set_parallel_enabled(bool enabled) {
    if (tick_system_) {
        tick_system_->set_parallel_enabled(enabled);
    }
}

bool GDTickSystem::get_parallel_enabled() const {
    return tick_system_ ? tick_system_->parallel_enabled() : false;
}

void GDTickSystem::set_max_worker_threads(int64_t count) {
    if (tick_system_) {
        tick_system_->set_max_worker_threads(static_cast<int>(count));
    }
}

int64_t GDTickSystem::get_max_worker_threads() const {
    return tick_system_ ? tick_system_->max_worker_threads() : 0;
}

int64_t GDTickSystem::get_tick_count() const {
    return tick_system_ ? tick_system_->tick_count() : 0;
}

void GDTickSystem::set_perf_profiler_enabled(bool enabled) {
    if (tick_system_) {
        tick_system_->set_profiler_enabled(enabled);
    }
}

bool GDTickSystem::get_perf_profiler_enabled() const {
    return tick_system_ ? tick_system_->profiler_enabled() : false;
}

void GDTickSystem::set_perf_profiler_tick_budget_ms(double budget_ms) {
    if (tick_system_) {
        tick_system_->set_profiler_tick_budget_ms(budget_ms);
    }
}

double GDTickSystem::get_perf_profiler_tick_budget_ms() const {
    if (!tick_system_) return 0.0;
    return tick_system_->profiler_config().tick_budget_ms;
}

void GDTickSystem::set_perf_profiler_slow_scope_ms(double threshold_ms) {
    if (tick_system_) {
        tick_system_->set_profiler_slow_scope_ms(threshold_ms);
    }
}

double GDTickSystem::get_perf_profiler_slow_scope_ms() const {
    if (!tick_system_) return 0.0;
    return tick_system_->profiler_config().slow_scope_ms;
}

void GDTickSystem::set_perf_profiler_log_interval_ticks(int64_t ticks) {
    if (tick_system_) {
        tick_system_->set_profiler_log_interval_ticks(ticks);
    }
}

int64_t GDTickSystem::get_perf_profiler_log_interval_ticks() const {
    if (!tick_system_) return 0;
    return tick_system_->profiler_config().log_interval_ticks;
}

godot::String GDTickSystem::get_perf_profiler_summary(int64_t top_n) const {
    if (!tick_system_) return godot::String();
    const size_t limit = top_n > 0 ? static_cast<size_t>(top_n) : 0;
    const std::string summary = tick_system_->format_profiler_summary(limit);
    return godot::String(summary.c_str());
}

godot::Array GDTickSystem::get_perf_profiler_top(int64_t top_n) const {
    godot::Array result;
    if (!tick_system_) return result;
    const size_t limit = top_n > 0 ? static_cast<size_t>(top_n) : 0;
    for (const auto& entry : tick_system_->profiler_snapshot_top(limit)) {
        godot::Dictionary d;
        d["name"] = godot::String(entry.name.c_str());
        d["last_ms"] = entry.last_ms;
        d["avg_ms"] = entry.avg_ms;
        d["max_ms"] = entry.max_ms;
        d["p99_ms"] = entry.p99_ms;
        d["budget_share"] = entry.budget_share;
        d["samples"] = entry.samples;
        d["slow_samples"] = entry.slow_samples;
        result.append(d);
    }
    return result;
}

void GDTickSystem::clear_perf_profiler() {
    if (tick_system_) {
        tick_system_->clear_profiler();
    }
}

int64_t GDTickSystem::get_active_chunk_count() const {
    if (!tick_system_) return 0;
    return static_cast<int64_t>(tick_system_->active_chunks().size());
}

godot::Array GDTickSystem::get_active_chunks() const {
    godot::Array arr;
    if (!tick_system_) return arr;
    for (const auto& key : tick_system_->active_chunks()) {
        arr.append(chunk_key_to_dict(key));
    }
    return arr;
}

godot::Array GDTickSystem::get_dirty_chunks() const {
    godot::Array arr;
    if (!tick_system_ || !tick_system_->state_sync()) return arr;
    auto dirty = tick_system_->state_sync()->dirty_chunks();
    for (const auto& key : dirty) {
        arr.append(chunk_key_to_dict(key));
    }
    return arr;
}

godot::Dictionary GDTickSystem::compute_delta_for(
    int64_t player_handle, const godot::Array& chunk_keys) {
    godot::Dictionary dict;
    if (!tick_system_ || !tick_system_->state_sync()) return dict;

    std::vector<ChunkKey> keys;
    for (int64_t i = 0; i < chunk_keys.size(); ++i) {
        auto d = chunk_keys[i];
        if (d.get_type() != godot::Variant::DICTIONARY) continue;
        godot::Dictionary kd = d;
        ChunkKey ck;
        ck.dimension_id = static_cast<godot::String>(kd["dimension"]).utf8().get_data();
        ck.chunk_x = static_cast<int>(kd["cx"]);
        ck.chunk_y = static_cast<int>(kd["cy"]);
        ck.chunk_z = static_cast<int>(kd["cz"]);
        keys.push_back(ck);
    }

    auto delta = tick_system_->state_sync()->compute_delta_for(
        static_cast<PlayerHandle>(player_handle), keys);
    return delta_to_dict(delta);
}

godot::Array GDTickSystem::compute_deltas_batch(const godot::Array& observer_views) {
    godot::Array results;
    if (!tick_system_ || !tick_system_->state_sync()) return results;

    std::vector<std::pair<PlayerHandle, std::vector<ChunkKey>>> views;
    views.reserve(static_cast<size_t>(observer_views.size()));

    for (int64_t i = 0; i < observer_views.size(); ++i) {
        const godot::Variant& v = observer_views[i];
        if (v.get_type() != godot::Variant::DICTIONARY) continue;
        godot::Dictionary view = v;
        if (!view.has("player_handle") || !view.has("chunks")) continue;

        PlayerHandle pid = static_cast<PlayerHandle>(
            static_cast<int64_t>(view["player_handle"]));
        std::vector<ChunkKey> keys;
        godot::Array chunks = view["chunks"];
        for (int64_t j = 0; j < chunks.size(); ++j) {
            auto d = chunks[j];
            if (d.get_type() != godot::Variant::DICTIONARY) continue;
            godot::Dictionary kd = d;
            ChunkKey ck;
            ck.dimension_id = static_cast<godot::String>(
                kd["dimension"]).utf8().get_data();
            ck.chunk_x = static_cast<int>(kd["cx"]);
            ck.chunk_y = static_cast<int>(kd["cy"]);
            ck.chunk_z = static_cast<int>(kd["cz"]);
            keys.push_back(ck);
        }
        views.emplace_back(pid, std::move(keys));
    }

    auto batch_results = tick_system_->state_sync()->compute_deltas_batch(views);
    for (const auto& [pid, delta] : batch_results) {
        godot::Dictionary entry;
        entry["player_handle"] = static_cast<int64_t>(pid);
        entry["delta"] = delta_to_dict(delta);
        results.append(entry);
    }
    return results;
}

godot::Dictionary GDTickSystem::create_snapshot(
    const godot::String& dimension, int cx, int cy, int cz) {
    godot::Dictionary dict;
    if (!tick_system_ || !tick_system_->state_sync()) return dict;

    ChunkKey key;
    key.dimension_id = dimension.utf8().get_data();
    key.chunk_x = cx;
    key.chunk_y = cy;
    key.chunk_z = cz;

    auto delta = tick_system_->state_sync()->create_snapshot(key);
    return delta_to_dict(delta);
}

WorldData* GDTickSystem::get_world_data_ptr() const {
    if (!gd_world_data_) return nullptr;
    auto* gd_world = reinterpret_cast<GDWorldData*>(gd_world_data_);
    return gd_world->get_world_ptr();
}

godot::Dictionary GDTickSystem::event_to_dict(const GameEvent& ev) const {
    godot::Dictionary d;
    d["type"] = static_cast<int64_t>(ev.type);
    d["source_id"] = static_cast<int64_t>(ev.source_id);
    d["source_dimension"] = godot::String(ev.source_dimension.c_str());
    d["cell_x"] = ev.cell_x;
    d["cell_y"] = ev.cell_y;
    d["cell_z"] = ev.cell_z;
    d["chunk_x"] = ev.chunk_x;
    d["chunk_y"] = ev.chunk_y;
    d["chunk_z"] = ev.chunk_z;
    d["timestamp"] = ev.timestamp;

    godot::Dictionary sd;
    for (const auto& p : ev.string_data) {
        sd[godot::String(p.first.c_str())] = godot::String(p.second.c_str());
    }
    d["string_data"] = sd;

    godot::Dictionary intd;
    for (const auto& p : ev.int_data) {
        intd[godot::String(p.first.c_str())] = p.second;
    }
    d["int_data"] = intd;

    godot::Dictionary floatd;
    for (const auto& p : ev.float_data) {
        floatd[godot::String(p.first.c_str())] = p.second;
    }
    d["float_data"] = floatd;

    return d;
}

godot::Dictionary GDTickSystem::chunk_key_to_dict(const ChunkKey& key) const {
    godot::Dictionary d;
    d["dimension"] = godot::String(key.dimension_id.c_str());
    d["cx"] = key.chunk_x;
    d["cy"] = key.chunk_y;
    d["cz"] = key.chunk_z;
    return d;
}

godot::Dictionary GDTickSystem::delta_to_dict(const StateDelta& delta) const {
    godot::Dictionary d;
    d["flags"] = static_cast<int64_t>(static_cast<uint32_t>(delta.flags));
    d["timestamp"] = delta.timestamp;

    godot::Array chunks;
    for (const auto& key : delta.chunks_modified) {
        chunks.append(chunk_key_to_dict(key));
    }
    d["chunks_modified"] = chunks;

    godot::Array created;
    for (const auto& eid : delta.entities_created) {
        created.append(static_cast<int64_t>(eid.id));
    }
    d["entities_created"] = created;

    godot::Array destroyed;
    for (const auto& eid : delta.entities_destroyed) {
        destroyed.append(static_cast<int64_t>(eid.id));
    }
    d["entities_destroyed"] = destroyed;

    godot::Array state_changes;
    for (const auto& [eid, new_state] : delta.machine_state_changes) {
        godot::Dictionary sc;
        sc["entity_id"] = static_cast<int64_t>(eid.id);
        sc["new_state"] = new_state;
        state_changes.append(sc);
    }
    d["machine_state_changes"] = state_changes;

    return d;
}

void GDTickSystem::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("set_world_data", "gd_world"),
        &GDTickSystem::set_world_data);
    godot::ClassDB::bind_method(godot::D_METHOD("register_tree_growth_system"),
        &GDTickSystem::register_tree_growth_system);
    godot::ClassDB::bind_method(godot::D_METHOD("register_crop_growth_system"),
        &GDTickSystem::register_crop_growth_system);
    godot::ClassDB::bind_method(godot::D_METHOD("register_season_system"),
        &GDTickSystem::register_season_system);
    godot::ClassDB::bind_method(godot::D_METHOD("register_day_night_system"),
        &GDTickSystem::register_day_night_system);
    godot::ClassDB::bind_method(godot::D_METHOD("register_region_system"),
        &GDTickSystem::register_region_system);
    godot::ClassDB::bind_method(godot::D_METHOD("register_ecosystem_system"),
        &GDTickSystem::register_ecosystem_system);
    godot::ClassDB::bind_method(godot::D_METHOD("get_day_night_state"),
        &GDTickSystem::get_day_night_state);
    godot::ClassDB::bind_method(godot::D_METHOD("get_time_of_day"),
        &GDTickSystem::get_time_of_day);
    godot::ClassDB::bind_method(godot::D_METHOD("get_is_daytime"),
        &GDTickSystem::get_is_daytime);
    godot::ClassDB::bind_method(godot::D_METHOD("get_region_count"),
        &GDTickSystem::get_region_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_region_count_by_type",
        "type_index"), &GDTickSystem::get_region_count_by_type);
    godot::ClassDB::bind_method(godot::D_METHOD("get_region_data",
        "type_index", "region_id"), &GDTickSystem::get_region_data);

    // Ecosystem query methods.
    godot::ClassDB::bind_method(godot::D_METHOD("get_population_data",
        "dimension", "cx", "cy", "cz"),
        &GDTickSystem::get_population_data);
    godot::ClassDB::bind_method(godot::D_METHOD("get_total_vegetation"),
        &GDTickSystem::get_total_vegetation);
    godot::ClassDB::bind_method(godot::D_METHOD("get_total_herbivore"),
        &GDTickSystem::get_total_herbivore);
    godot::ClassDB::bind_method(godot::D_METHOD("get_total_predator"),
        &GDTickSystem::get_total_predator);
    godot::ClassDB::bind_method(godot::D_METHOD("get_total_proxy_count"),
        &GDTickSystem::get_total_proxy_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_proxy_data",
        "dimension", "cx", "cy", "cz"),
        &GDTickSystem::get_proxy_data);
    godot::ClassDB::bind_method(godot::D_METHOD("sync_ecosystem_to_chunks"),
        &GDTickSystem::sync_ecosystem_to_chunks);
    godot::ClassDB::bind_method(godot::D_METHOD("restore_ecosystem_from_chunks"),
        &GDTickSystem::restore_ecosystem_from_chunks);
    godot::ClassDB::bind_method(godot::D_METHOD("attack_creature",
        "dimension", "player_pos", "look_dir", "reach", "damage"),
        &GDTickSystem::attack_creature);
    godot::ClassDB::bind_method(godot::D_METHOD("get_species_drops",
        "species_id"), &GDTickSystem::get_species_drops);
    godot::ClassDB::bind_method(godot::D_METHOD("feed_creatures",
        "dimension", "cx", "cy", "cz", "role", "amount"),
        &GDTickSystem::feed_creatures);
    godot::ClassDB::bind_method(godot::D_METHOD("feed_creature_at",
        "dimension", "player_pos", "look_dir", "reach"),
        &GDTickSystem::feed_creature_at);
    godot::ClassDB::bind_method(godot::D_METHOD("get_total_captive_count"),
        &GDTickSystem::get_total_captive_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_captive_data",
        "dimension", "cx", "cy", "cz"),
        &GDTickSystem::get_captive_data);
    godot::ClassDB::bind_method(godot::D_METHOD("tick", "delta"),
        &GDTickSystem::tick);
    godot::ClassDB::bind_method(godot::D_METHOD("add_player_chunk", "player_handle",
        "dimension", "cx", "cy", "cz"), &GDTickSystem::add_player_chunk);
    godot::ClassDB::bind_method(godot::D_METHOD("remove_player_chunk", "player_handle"),
        &GDTickSystem::remove_player_chunk);
    godot::ClassDB::bind_method(godot::D_METHOD("clear_player_chunks"),
        &GDTickSystem::clear_player_chunks);
    godot::ClassDB::bind_method(godot::D_METHOD("get_player_count"),
        &GDTickSystem::get_player_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_player_dimension",
        "player_handle"), &GDTickSystem::get_player_dimension);

    godot::ClassDB::bind_method(godot::D_METHOD("get_active_radius"),
        &GDTickSystem::get_active_radius);
    godot::ClassDB::bind_method(godot::D_METHOD("set_active_radius", "radius"),
        &GDTickSystem::set_active_radius);

    // Sleep interval configuration.
    godot::ClassDB::bind_method(godot::D_METHOD("get_sleep_near_interval"),
        &GDTickSystem::get_sleep_near_interval);
    godot::ClassDB::bind_method(godot::D_METHOD("set_sleep_near_interval",
        "interval"), &GDTickSystem::set_sleep_near_interval);
    godot::ClassDB::bind_method(godot::D_METHOD("get_sleep_mid_interval"),
        &GDTickSystem::get_sleep_mid_interval);
    godot::ClassDB::bind_method(godot::D_METHOD("set_sleep_mid_interval",
        "interval"), &GDTickSystem::set_sleep_mid_interval);
    godot::ClassDB::bind_method(godot::D_METHOD("get_sleep_far_interval"),
        &GDTickSystem::get_sleep_far_interval);
    godot::ClassDB::bind_method(godot::D_METHOD("set_sleep_far_interval",
        "interval"), &GDTickSystem::set_sleep_far_interval);

    // Parallel execution control.
    godot::ClassDB::bind_method(godot::D_METHOD("set_parallel_enabled",
        "enabled"), &GDTickSystem::set_parallel_enabled);
    godot::ClassDB::bind_method(godot::D_METHOD("get_parallel_enabled"),
        &GDTickSystem::get_parallel_enabled);
    godot::ClassDB::bind_method(godot::D_METHOD("set_max_worker_threads",
        "count"), &GDTickSystem::set_max_worker_threads);
    godot::ClassDB::bind_method(godot::D_METHOD("get_max_worker_threads"),
        &GDTickSystem::get_max_worker_threads);

    godot::ClassDB::bind_method(godot::D_METHOD("get_tick_count"),
        &GDTickSystem::get_tick_count);

    // Tick profiler command API.
    godot::ClassDB::bind_method(godot::D_METHOD("set_perf_profiler_enabled",
        "enabled"), &GDTickSystem::set_perf_profiler_enabled);
    godot::ClassDB::bind_method(godot::D_METHOD("get_perf_profiler_enabled"),
        &GDTickSystem::get_perf_profiler_enabled);
    godot::ClassDB::bind_method(godot::D_METHOD("set_perf_profiler_tick_budget_ms",
        "budget_ms"), &GDTickSystem::set_perf_profiler_tick_budget_ms);
    godot::ClassDB::bind_method(godot::D_METHOD("get_perf_profiler_tick_budget_ms"),
        &GDTickSystem::get_perf_profiler_tick_budget_ms);
    godot::ClassDB::bind_method(godot::D_METHOD("set_perf_profiler_slow_scope_ms",
        "threshold_ms"), &GDTickSystem::set_perf_profiler_slow_scope_ms);
    godot::ClassDB::bind_method(godot::D_METHOD("get_perf_profiler_slow_scope_ms"),
        &GDTickSystem::get_perf_profiler_slow_scope_ms);
    godot::ClassDB::bind_method(godot::D_METHOD("set_perf_profiler_log_interval_ticks",
        "ticks"), &GDTickSystem::set_perf_profiler_log_interval_ticks);
    godot::ClassDB::bind_method(godot::D_METHOD("get_perf_profiler_log_interval_ticks"),
        &GDTickSystem::get_perf_profiler_log_interval_ticks);
    godot::ClassDB::bind_method(godot::D_METHOD("get_perf_profiler_summary",
        "top_n"), &GDTickSystem::get_perf_profiler_summary);
    godot::ClassDB::bind_method(godot::D_METHOD("get_perf_profiler_top",
        "top_n"), &GDTickSystem::get_perf_profiler_top);
    godot::ClassDB::bind_method(godot::D_METHOD("clear_perf_profiler"),
        &GDTickSystem::clear_perf_profiler);

    godot::ClassDB::bind_method(godot::D_METHOD("get_active_chunk_count"),
        &GDTickSystem::get_active_chunk_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_active_chunks"),
        &GDTickSystem::get_active_chunks);

    godot::ClassDB::bind_method(godot::D_METHOD("get_dirty_chunks"),
        &GDTickSystem::get_dirty_chunks);
    godot::ClassDB::bind_method(godot::D_METHOD("compute_delta_for",
        "player_handle", "chunk_keys"), &GDTickSystem::compute_delta_for);
    godot::ClassDB::bind_method(godot::D_METHOD("compute_deltas_batch",
        "observer_views"), &GDTickSystem::compute_deltas_batch);
    godot::ClassDB::bind_method(godot::D_METHOD("create_snapshot", "dimension",
        "cx", "cy", "cz"), &GDTickSystem::create_snapshot);

    // --- Signals: real-time events bridged from EventBus ---

    ADD_SIGNAL(godot::MethodInfo("power_overload",
        godot::PropertyInfo(godot::Variant::INT, "node_id"),
        godot::PropertyInfo(godot::Variant::INT, "demand"),
        godot::PropertyInfo(godot::Variant::INT, "capacity")));
    ADD_SIGNAL(godot::MethodInfo("chunk_generated",
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("chunk_state_changed",
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz"),
        godot::PropertyInfo(godot::Variant::INT, "old_state"),
        godot::PropertyInfo(godot::Variant::INT, "new_state")));
    ADD_SIGNAL(godot::MethodInfo("entity_created",
        godot::PropertyInfo(godot::Variant::INT, "entity_id"),
        godot::PropertyInfo(godot::Variant::STRING, "type_name"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("entity_destroyed",
        godot::PropertyInfo(godot::Variant::INT, "entity_id"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension")));

    ADD_SIGNAL(godot::MethodInfo("terrain_changed",
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz"),
        godot::PropertyInfo(godot::Variant::INT, "x"),
        godot::PropertyInfo(godot::Variant::INT, "y"),
        godot::PropertyInfo(godot::Variant::INT, "z"),
        godot::PropertyInfo(godot::Variant::INT, "old_material"),
        godot::PropertyInfo(godot::Variant::INT, "new_material")));

    ADD_SIGNAL(godot::MethodInfo("item_dropped",
        godot::PropertyInfo(godot::Variant::INT, "item_id"),
        godot::PropertyInfo(godot::Variant::INT, "count"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz"),
        godot::PropertyInfo(godot::Variant::INT, "x"),
        godot::PropertyInfo(godot::Variant::INT, "y"),
        godot::PropertyInfo(godot::Variant::INT, "z")));

    ADD_SIGNAL(godot::MethodInfo("item_picked_up",
        godot::PropertyInfo(godot::Variant::INT, "item_id"),
        godot::PropertyInfo(godot::Variant::INT, "count"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension")));

    ADD_SIGNAL(godot::MethodInfo("entity_damaged",
        godot::PropertyInfo(godot::Variant::INT, "entity_id"),
        godot::PropertyInfo(godot::Variant::FLOAT, "damage"),
        godot::PropertyInfo(godot::Variant::STRING, "source_dimension")));

    ADD_SIGNAL(godot::MethodInfo("player_inventory_changed"));

    ADD_SIGNAL(godot::MethodInfo("player_equipment_changed",
        godot::PropertyInfo(godot::Variant::INT, "slot_type"),
        godot::PropertyInfo(godot::Variant::INT, "old_item_id"),
        godot::PropertyInfo(godot::Variant::INT, "new_item_id")));

    // --- Region signals ---

    ADD_SIGNAL(godot::MethodInfo("region_created",
        godot::PropertyInfo(godot::Variant::INT, "region_id"),
        godot::PropertyInfo(godot::Variant::STRING, "region_type"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension")));
    ADD_SIGNAL(godot::MethodInfo("region_destroyed",
        godot::PropertyInfo(godot::Variant::INT, "region_id"),
        godot::PropertyInfo(godot::Variant::STRING, "region_type"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension")));
    ADD_SIGNAL(godot::MethodInfo("region_merged",
        godot::PropertyInfo(godot::Variant::INT, "merged_id"),
        godot::PropertyInfo(godot::Variant::INT, "absorbed_id"),
        godot::PropertyInfo(godot::Variant::STRING, "region_type"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension")));
    ADD_SIGNAL(godot::MethodInfo("region_split",
        godot::PropertyInfo(godot::Variant::INT, "original_id"),
        godot::PropertyInfo(godot::Variant::INT, "new_id"),
        godot::PropertyInfo(godot::Variant::STRING, "region_type"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension")));
    ADD_SIGNAL(godot::MethodInfo("region_pollution_changed",
        godot::PropertyInfo(godot::Variant::INT, "region_id"),
        godot::PropertyInfo(godot::Variant::FLOAT, "old_level"),
        godot::PropertyInfo(godot::Variant::FLOAT, "new_level"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension")));
    ADD_SIGNAL(godot::MethodInfo("region_temperature_changed",
        godot::PropertyInfo(godot::Variant::INT, "region_id"),
        godot::PropertyInfo(godot::Variant::FLOAT, "old_temp"),
        godot::PropertyInfo(godot::Variant::FLOAT, "new_temp"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension")));

    // Ecosystem signals.
    ADD_SIGNAL(godot::MethodInfo("creature_spawned",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::STRING, "species_key"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("creature_despawned",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::STRING, "species_key"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("creature_damaged",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::INT, "species_id"),
        godot::PropertyInfo(godot::Variant::FLOAT, "damage"),
        godot::PropertyInfo(godot::Variant::FLOAT, "remaining_health"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("creature_killed",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::INT, "species_id"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("creature_moved",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::STRING, "species_key"),
        godot::PropertyInfo(godot::Variant::FLOAT, "pos_x"),
        godot::PropertyInfo(godot::Variant::FLOAT, "pos_y"),
        godot::PropertyInfo(godot::Variant::FLOAT, "pos_z")));

    // Captive / husbandry signals.
    ADD_SIGNAL(godot::MethodInfo("creature_captured",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::INT, "species_id"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("creature_tamed",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::INT, "species_id"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("creature_bred",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::INT, "species_id"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("creature_grown",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::INT, "species_id"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("captive_creature_added",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::STRING, "species_key"),
        godot::PropertyInfo(godot::Variant::INT, "species_id"),
        godot::PropertyInfo(godot::Variant::INT, "age_stage"),
        godot::PropertyInfo(godot::Variant::INT, "is_tamed"),
        godot::PropertyInfo(godot::Variant::FLOAT, "pos_x"),
        godot::PropertyInfo(godot::Variant::FLOAT, "pos_y"),
        godot::PropertyInfo(godot::Variant::FLOAT, "pos_z"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("captive_creature_removed",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::STRING, "dimension"),
        godot::PropertyInfo(godot::Variant::INT, "cx"),
        godot::PropertyInfo(godot::Variant::INT, "cy"),
        godot::PropertyInfo(godot::Variant::INT, "cz")));
    ADD_SIGNAL(godot::MethodInfo("captive_creature_moved",
        godot::PropertyInfo(godot::Variant::INT, "creature_id"),
        godot::PropertyInfo(godot::Variant::STRING, "species_key"),
        godot::PropertyInfo(godot::Variant::FLOAT, "pos_x"),
        godot::PropertyInfo(godot::Variant::FLOAT, "pos_y"),
        godot::PropertyInfo(godot::Variant::FLOAT, "pos_z")));
}

void GDTickSystem::subscribe_to_event_bus() {
    if (!tick_system_) return;
    auto* bus = tick_system_->event_bus();
    if (!bus) return;

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::POWER_OVERLOAD,
        [this](const GameEvent& e) {
            emit_signal("power_overload",
                static_cast<int64_t>(e.source_id),
                e.int_data.at("demand"),
                e.int_data.at("capacity"));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CHUNK_GENERATED,
        [this](const GameEvent& e) {
            emit_signal("chunk_generated",
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CHUNK_STATE_CHANGED,
        [this](const GameEvent& e) {
            emit_signal("chunk_state_changed",
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z,
                e.int_data.at("old_state"),
                e.int_data.at("new_state"));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::ENTITY_CREATED,
        [this](const GameEvent& e) {
            emit_signal("entity_created",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("type_name").c_str()),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::ENTITY_DESTROYED,
        [this](const GameEvent& e) {
            emit_signal("entity_destroyed",
                static_cast<int64_t>(e.source_id),
                godot::String(e.source_dimension.c_str()));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::TERRAIN_CHANGED,
        [this](const GameEvent& e) {
            emit_signal("terrain_changed",
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z,
                e.cell_x, e.cell_y, e.cell_z,
                e.int_data.at("old_material"),
                e.int_data.at("new_material"));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::ITEM_DROPPED,
        [this](const GameEvent& e) {
            emit_signal("item_dropped",
                static_cast<int64_t>(e.source_id),
                e.int_data.at("count"),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z,
                e.cell_x, e.cell_y, e.cell_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::ITEM_PICKED_UP,
        [this](const GameEvent& e) {
            emit_signal("item_picked_up",
                static_cast<int64_t>(e.source_id),
                e.int_data.at("count"),
                godot::String(e.source_dimension.c_str()));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::ENTITY_DAMAGED,
        [this](const GameEvent& e) {
            emit_signal("entity_damaged",
                static_cast<int64_t>(e.source_id),
                e.float_data.at("damage"),
                godot::String(e.source_dimension.c_str()));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::PLAYER_INVENTORY_CHANGED,
        [this](const GameEvent&) {
            emit_signal("player_inventory_changed");
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::PLAYER_EQUIPMENT_CHANGED,
        [this](const GameEvent& e) {
            emit_signal("player_equipment_changed",
                e.int_data.at("slot_type"),
                e.int_data.at("old_item_id"),
                e.int_data.at("new_item_id"));
        }));

    // --- Region event subscriptions ---

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::REGION_CREATED,
        [this](const GameEvent& e) {
            emit_signal("region_created",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("region_type").c_str()),
                godot::String(e.source_dimension.c_str()));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::REGION_DESTROYED,
        [this](const GameEvent& e) {
            emit_signal("region_destroyed",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("region_type").c_str()),
                godot::String(e.source_dimension.c_str()));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::REGION_MERGED,
        [this](const GameEvent& e) {
            emit_signal("region_merged",
                static_cast<int64_t>(e.source_id),
                e.int_data.at("absorbed_id"),
                godot::String(e.string_data.at("region_type").c_str()),
                godot::String(e.source_dimension.c_str()));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::REGION_SPLIT,
        [this](const GameEvent& e) {
            emit_signal("region_split",
                static_cast<int64_t>(e.source_id),
                e.int_data.at("new_id"),
                godot::String(e.string_data.at("region_type").c_str()),
                godot::String(e.source_dimension.c_str()));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::REGION_POLLUTION_CHANGED,
        [this](const GameEvent& e) {
            emit_signal("region_pollution_changed",
                static_cast<int64_t>(e.source_id),
                e.float_data.at("old_level"),
                e.float_data.at("new_level"),
                godot::String(e.source_dimension.c_str()));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::REGION_TEMPERATURE_CHANGED,
        [this](const GameEvent& e) {
            emit_signal("region_temperature_changed",
                static_cast<int64_t>(e.source_id),
                e.float_data.at("old_temp"),
                e.float_data.at("new_temp"),
                godot::String(e.source_dimension.c_str()));
        }));

    // --- Ecosystem event subscriptions ---

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CREATURE_SPAWNED,
        [this](const GameEvent& e) {
            emit_signal("creature_spawned",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("species_key").c_str()),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CREATURE_DESPAWNED,
        [this](const GameEvent& e) {
            emit_signal("creature_despawned",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("species_key").c_str()),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CREATURE_DAMAGED,
        [this](const GameEvent& e) {
            emit_signal("creature_damaged",
                static_cast<int64_t>(e.source_id),
                static_cast<int64_t>(e.int_data.at("species_id")),
                static_cast<float>(e.float_data.at("damage")),
                static_cast<float>(e.float_data.at("remaining_health")),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CREATURE_KILLED,
        [this](const GameEvent& e) {
            emit_signal("creature_killed",
                static_cast<int64_t>(e.source_id),
                static_cast<int64_t>(e.int_data.at("species_id")),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CREATURE_MOVED,
        [this](const GameEvent& e) {
            emit_signal("creature_moved",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("species_key").c_str()),
                static_cast<float>(e.float_data.at("pos_x")),
                static_cast<float>(e.float_data.at("pos_y")),
                static_cast<float>(e.float_data.at("pos_z")));
        }));

    // --- Captive / husbandry event subscriptions ---

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CREATURE_CAPTURED,
        [this](const GameEvent& e) {
            emit_signal("creature_captured",
                static_cast<int64_t>(e.source_id),
                static_cast<int64_t>(e.int_data.at("species_id")),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CREATURE_TAMED,
        [this](const GameEvent& e) {
            emit_signal("creature_tamed",
                static_cast<int64_t>(e.source_id),
                static_cast<int64_t>(e.int_data.at("species_id")),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CREATURE_BRED,
        [this](const GameEvent& e) {
            emit_signal("creature_bred",
                static_cast<int64_t>(e.source_id),
                static_cast<int64_t>(e.int_data.at("species_id")),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CREATURE_GROWN,
        [this](const GameEvent& e) {
            emit_signal("creature_grown",
                static_cast<int64_t>(e.source_id),
                static_cast<int64_t>(e.int_data.at("species_id")),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CAPTIVE_CREATURE_ADDED,
        [this](const GameEvent& e) {
            emit_signal("captive_creature_added",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("species_key").c_str()),
                static_cast<int64_t>(e.int_data.at("species_id")),
                e.int_data.at("age_stage"),
                e.int_data.at("is_tamed"),
                static_cast<float>(e.float_data.at("pos_x")),
                static_cast<float>(e.float_data.at("pos_y")),
                static_cast<float>(e.float_data.at("pos_z")),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CAPTIVE_CREATURE_REMOVED,
        [this](const GameEvent& e) {
            emit_signal("captive_creature_removed",
                static_cast<int64_t>(e.source_id),
                godot::String(e.source_dimension.c_str()),
                e.chunk_x, e.chunk_y, e.chunk_z);
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::CAPTIVE_CREATURE_MOVED,
        [this](const GameEvent& e) {
            emit_signal("captive_creature_moved",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("species_key").c_str()),
                static_cast<float>(e.float_data.at("pos_x")),
                static_cast<float>(e.float_data.at("pos_y")),
                static_cast<float>(e.float_data.at("pos_z")));
        }));
}

void GDTickSystem::unsubscribe_from_event_bus() {
    if (!tick_system_) return;
    auto* bus = tick_system_->event_bus();
    if (!bus) return;
    for (auto id : event_subscriptions_) {
        bus->unsubscribe(id);
    }
    event_subscriptions_.clear();
}

} // namespace science_and_theology
