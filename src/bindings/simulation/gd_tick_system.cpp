#include "gd_tick_system.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/resource.hpp>

#include "core/simulation/event_types.hpp"
#include "core/simulation/machine_system.hpp"
#include "core/simulation/block_physics_system.hpp"
#include "core/simulation/tree_growth_system.hpp"
#include "core/simulation/season_system.hpp"
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

void GDTickSystem::register_machine_system() {
    if (tick_system_) {
        tick_system_->register_subsystem(std::make_unique<MachineSystem>());
    }
}

void GDTickSystem::register_block_physics_system() {
    if (tick_system_) {
        tick_system_->register_subsystem(std::make_unique<BlockPhysicsSystem>());
    }
}

void GDTickSystem::register_tree_growth_system() {
    if (tick_system_) {
        tick_system_->register_subsystem(std::make_unique<TreeGrowthSystem>());
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

void GDTickSystem::tick(float delta) {
    if (!tick_system_ || !world_set) return;
    tick_system_->tick(delta);
}

void GDTickSystem::set_player_chunk(
    const godot::String& dimension, int cx, int cy, int cz) {
    if (tick_system_ && world_set) {
        tick_system_->set_player_chunk(dimension.utf8().get_data(), cx, cy, cz);
    }
}

int64_t GDTickSystem::get_active_radius() const {
    return tick_system_ ? tick_system_->active_radius() : 0;
}

void GDTickSystem::set_active_radius(int64_t radius) {
    if (tick_system_) {
        tick_system_->set_active_radius(static_cast<int>(radius));
    }
}

int64_t GDTickSystem::get_tick_count() const {
    return tick_system_ ? tick_system_->tick_count() : 0;
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

godot::Array GDTickSystem::poll_events() {
    godot::Array arr;
    // EventBus drains its queue during process_queue() in tick().
    // For GDScript, events are typically handled via signals.
    // This method is a polling alternative for deferred inspection.
    return arr;
}

godot::Array GDTickSystem::get_machine_errors() const {
    godot::Array arr;
    if (!tick_system_ || !tick_system_->error_handler()) return arr;
    auto errors = tick_system_->error_handler()->get_all_errors();
    for (const auto& err : errors) {
        arr.append(error_to_dict(err));
    }
    return arr;
}

void GDTickSystem::clear_machine_error(int64_t machine_id) {
    if (tick_system_ && tick_system_->error_handler()) {
        MachineId mid;
        mid.id = static_cast<uint64_t>(machine_id);
        tick_system_->error_handler()->clear_error(mid);
    }
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

godot::Dictionary GDTickSystem::compute_delta(const godot::Array& chunk_keys) {
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

    auto delta = tick_system_->state_sync()->compute_delta(keys);
    return delta_to_dict(delta);
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

void GDTickSystem::set_player_inventory(godot::Resource* inventory) {
    player_inventory_ = inventory;
}

void GDTickSystem::set_player_equipment(godot::Resource* equipment) {
    player_equipment_ = equipment;
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

godot::Dictionary GDTickSystem::error_to_dict(const MachineError& err) const {
    godot::Dictionary d;
    d["machine_id"] = static_cast<int64_t>(err.machine_id.id);
    d["error_code"] = godot::String(err.error_code.c_str());
    d["message"] = godot::String(err.message.c_str());
    d["severity"] = static_cast<int64_t>(err.severity);
    d["timestamp"] = err.timestamp;
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
    godot::ClassDB::bind_method(godot::D_METHOD("register_machine_system"),
        &GDTickSystem::register_machine_system);
    godot::ClassDB::bind_method(godot::D_METHOD("register_block_physics_system"),
        &GDTickSystem::register_block_physics_system);
    godot::ClassDB::bind_method(godot::D_METHOD("register_tree_growth_system"),
        &GDTickSystem::register_tree_growth_system);
    godot::ClassDB::bind_method(godot::D_METHOD("register_season_system"),
        &GDTickSystem::register_season_system);
    godot::ClassDB::bind_method(godot::D_METHOD("register_day_night_system"),
        &GDTickSystem::register_day_night_system);
    godot::ClassDB::bind_method(godot::D_METHOD("get_day_night_state"),
        &GDTickSystem::get_day_night_state);
    godot::ClassDB::bind_method(godot::D_METHOD("get_time_of_day"),
        &GDTickSystem::get_time_of_day);
    godot::ClassDB::bind_method(godot::D_METHOD("get_is_daytime"),
        &GDTickSystem::get_is_daytime);
    godot::ClassDB::bind_method(godot::D_METHOD("tick", "delta"),
        &GDTickSystem::tick);
    godot::ClassDB::bind_method(godot::D_METHOD("set_player_chunk", "dimension",
        "cx", "cy", "cz"), &GDTickSystem::set_player_chunk);

    godot::ClassDB::bind_method(godot::D_METHOD("get_active_radius"),
        &GDTickSystem::get_active_radius);
    godot::ClassDB::bind_method(godot::D_METHOD("set_active_radius", "radius"),
        &GDTickSystem::set_active_radius);
    godot::ClassDB::bind_method(godot::D_METHOD("get_tick_count"),
        &GDTickSystem::get_tick_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_active_chunk_count"),
        &GDTickSystem::get_active_chunk_count);
    godot::ClassDB::bind_method(godot::D_METHOD("get_active_chunks"),
        &GDTickSystem::get_active_chunks);

    // Deprecated: prefer signals instead.
    godot::ClassDB::bind_method(godot::D_METHOD("poll_events"),
        &GDTickSystem::poll_events);
    godot::ClassDB::bind_method(godot::D_METHOD("get_machine_errors"),
        &GDTickSystem::get_machine_errors);
    godot::ClassDB::bind_method(godot::D_METHOD("clear_machine_error",
        "machine_id"), &GDTickSystem::clear_machine_error);

    godot::ClassDB::bind_method(godot::D_METHOD("get_dirty_chunks"),
        &GDTickSystem::get_dirty_chunks);
    godot::ClassDB::bind_method(godot::D_METHOD("compute_delta",
        "chunk_keys"), &GDTickSystem::compute_delta);
    godot::ClassDB::bind_method(godot::D_METHOD("create_snapshot", "dimension",
        "cx", "cy", "cz"), &GDTickSystem::create_snapshot);

    godot::ClassDB::bind_method(godot::D_METHOD("set_player_inventory",
        "inventory"), &GDTickSystem::set_player_inventory);
    godot::ClassDB::bind_method(godot::D_METHOD("set_player_equipment",
        "equipment"), &GDTickSystem::set_player_equipment);

    // --- Signals: real-time events bridged from EventBus ---

    ADD_SIGNAL(godot::MethodInfo("machine_state_changed",
        godot::PropertyInfo(godot::Variant::INT, "machine_id"),
        godot::PropertyInfo(godot::Variant::INT, "old_state"),
        godot::PropertyInfo(godot::Variant::INT, "new_state")));
    ADD_SIGNAL(godot::MethodInfo("machine_recipe_completed",
        godot::PropertyInfo(godot::Variant::INT, "machine_id"),
        godot::PropertyInfo(godot::Variant::STRING, "recipe_name")));
    ADD_SIGNAL(godot::MethodInfo("machine_error",
        godot::PropertyInfo(godot::Variant::INT, "machine_id"),
        godot::PropertyInfo(godot::Variant::STRING, "error_code"),
        godot::PropertyInfo(godot::Variant::STRING, "message")));
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
}

void GDTickSystem::subscribe_to_event_bus() {
    if (!tick_system_) return;
    auto* bus = tick_system_->event_bus();
    if (!bus) return;

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::MACHINE_STATE_CHANGED,
        [this](const GameEvent& e) {
            emit_signal("machine_state_changed",
                static_cast<int64_t>(e.source_id),
                e.int_data.at("old_state"),
                e.int_data.at("new_state"));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::MACHINE_RECIPE_COMPLETED,
        [this](const GameEvent& e) {
            emit_signal("machine_recipe_completed",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("recipe_name").c_str()));
        }));

    event_subscriptions_.push_back(bus->subscribe(
        GameEventType::MACHINE_ERROR,
        [this](const GameEvent& e) {
            emit_signal("machine_error",
                static_cast<int64_t>(e.source_id),
                godot::String(e.string_data.at("error_code").c_str()),
                godot::String(e.string_data.at("message").c_str()));
        }));

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
