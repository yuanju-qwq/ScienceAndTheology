#include "dropped_organ_registry.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::source_law {

static std::vector<DroppedOrganDef> g_dropped_organs;
static std::vector<std::string> g_dropped_organ_name_storage;
static std::unordered_map<std::string, DroppedOrganId> g_dropped_organ_name_map;

void DroppedOrganRegistry::initialize() {
    g_dropped_organs.clear();
    g_dropped_organ_name_storage.clear();
    g_dropped_organ_name_map.clear();

    // Reserve ID 0 as invalid.
    g_dropped_organs.push_back({});
    g_dropped_organ_name_storage.push_back("__invalid__");

    // Built-in organs are now registered from GDScript via
    // GDDroppedOrganRegistry / BuiltinDroppedOrgans.
}

const DroppedOrganDef* DroppedOrganRegistry::get_by_id(DroppedOrganId id) {
    if (id == kInvalidDroppedOrganId || id >= g_dropped_organs.size()) return nullptr;
    return &g_dropped_organs[id];
}

const DroppedOrganDef* DroppedOrganRegistry::get_by_name(const char* name) {
    auto it = g_dropped_organ_name_map.find(name);
    if (it == g_dropped_organ_name_map.end()) return nullptr;
    return get_by_id(it->second);
}

std::vector<DroppedOrganId> DroppedOrganRegistry::find_by_creature(
    const char* creature_id) {
    std::vector<DroppedOrganId> result;
    for (size_t i = 1; i < g_dropped_organs.size(); ++i) {
        if (g_dropped_organs[i].source_creature_id != nullptr &&
            std::string(g_dropped_organs[i].source_creature_id) == creature_id) {
            result.push_back(static_cast<DroppedOrganId>(i));
        }
    }
    return result;
}

std::vector<DroppedOrganId> DroppedOrganRegistry::find_by_slot(OrganSlot slot) {
    std::vector<DroppedOrganId> result;
    for (size_t i = 1; i < g_dropped_organs.size(); ++i) {
        if (g_dropped_organs[i].target_slot == slot) {
            result.push_back(static_cast<DroppedOrganId>(i));
        }
    }
    return result;
}

std::vector<DroppedOrganId> DroppedOrganRegistry::find_by_imitated_path(
    SublimationPath path) {
    std::vector<DroppedOrganId> result;
    for (size_t i = 1; i < g_dropped_organs.size(); ++i) {
        if (g_dropped_organs[i].source == BloodlineSource::ABERRATION &&
            g_dropped_organs[i].imitated_path == path) {
            result.push_back(static_cast<DroppedOrganId>(i));
        }
    }
    return result;
}

size_t DroppedOrganRegistry::count() {
    return g_dropped_organs.size() > 0 ? g_dropped_organs.size() - 1 : 0;
}

DroppedOrganId DroppedOrganRegistry::register_organ(const DroppedOrganDef& def) {
    g_dropped_organ_name_storage.push_back(def.id);
    DroppedOrganDef stored = def;
    stored.id = g_dropped_organ_name_storage.back().c_str();

    // Store title_key in stable storage.
    g_dropped_organ_name_storage.push_back(def.title_key);
    stored.title_key = g_dropped_organ_name_storage.back().c_str();

    // Also store source_creature_id in stable storage.
    g_dropped_organ_name_storage.push_back(def.source_creature_id);
    stored.source_creature_id = g_dropped_organ_name_storage.back().c_str();

    DroppedOrganId id = static_cast<DroppedOrganId>(g_dropped_organs.size());
    g_dropped_organs.push_back(stored);
    g_dropped_organ_name_map[stored.id] = id;
    return id;
}

// ============================================================
// Built-in dropped organs
// ============================================================
//
// Migrated to GDScript (scripts/content/BuiltinDroppedOrgans.gd).
// This function is kept as a no-op for backwards compatibility.

void DroppedOrganRegistry::register_builtin_organs() {
    // No-op: built-in organs are registered from GDScript via
    // GDDroppedOrganRegistry / BuiltinDroppedOrgans.
}

} // namespace science_and_theology::source_law
