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

    register_builtin_organs();
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
// Creature-sourced organs: dropped by normal creatures.
// Aberration-sourced organs: dropped by aberrations, imitate
// a sublimation path organ but weaker.

void DroppedOrganRegistry::register_builtin_organs() {
    // --- Creature-sourced organs ---

    // Rock Lizard Scale Heart
    register_organ(DroppedOrganDef{
        "dropped_rock_lizard_heart",
        "Rock Lizard Source Heart",
        OrganSlot::HEART,
        BloodlineSource::CREATURE,
        "rock_lizard",
        magic::RuneElement::EARTH,
        {},
        SublimationPath::NONE,
        30,
        -2.0f,
        1.0f,
        OrganQuality::COMMON,
        0.6f
    });

    // Rock Lizard Bone
    register_organ(DroppedOrganDef{
        "dropped_rock_lizard_bone",
        "Rock Lizard Source Bone",
        OrganSlot::BONE,
        BloodlineSource::CREATURE,
        "rock_lizard",
        magic::RuneElement::EARTH,
        {},
        SublimationPath::NONE,
        30,
        -2.0f,
        1.0f,
        OrganQuality::COMMON,
        0.6f
    });

    // Sea Serpent Lung
    register_organ(DroppedOrganDef{
        "dropped_sea_serpent_lung",
        "Sea Serpent Source Lung",
        OrganSlot::LUNG,
        BloodlineSource::CREATURE,
        "sea_serpent",
        magic::RuneElement::WATER,
        {},
        SublimationPath::NONE,
        30,
        -2.0f,
        1.0f,
        OrganQuality::COMMON,
        0.6f
    });

    // --- Aberration-sourced organs (imitate sublimation paths) ---

    // Sand Armor Aberrant Bone
    register_organ(DroppedOrganDef{
        "dropped_aberrant_sand_armor_bone",
        "Aberrant Sand Armor Bone",
        OrganSlot::BONE,
        BloodlineSource::ABERRATION,
        "sand_armor_aberrant",
        magic::RuneElement::EARTH,
        {magic::RuneElement::ORDER},
        SublimationPath::SAND_ARMOR,
        40,
        -3.0f,
        2.0f,
        OrganQuality::FLAWED,
        0.5f
    });

    // Tidal Aberrant Lung
    register_organ(DroppedOrganDef{
        "dropped_aberrant_tidal_lung",
        "Aberrant Tidal Lung",
        OrganSlot::LUNG,
        BloodlineSource::ABERRATION,
        "tidal_aberrant",
        magic::RuneElement::WATER,
        {magic::RuneElement::LIGHT},
        SublimationPath::TIDAL,
        40,
        -3.0f,
        2.0f,
        OrganQuality::FLAWED,
        0.5f
    });

    // Storm Aberrant Nerve
    register_organ(DroppedOrganDef{
        "dropped_aberrant_storm_nerve",
        "Aberrant Storm Nerve",
        OrganSlot::NERVE,
        BloodlineSource::ABERRATION,
        "storm_aberrant",
        magic::RuneElement::AIR,
        {magic::RuneElement::FIRE},
        SublimationPath::STORM,
        40,
        -3.0f,
        2.0f,
        OrganQuality::FLAWED,
        0.5f
    });

    // Furnace Aberrant Heart
    register_organ(DroppedOrganDef{
        "dropped_aberrant_furnace_heart",
        "Aberrant Furnace Heart",
        OrganSlot::HEART,
        BloodlineSource::ABERRATION,
        "furnace_aberrant",
        magic::RuneElement::FIRE,
        {magic::RuneElement::CHAOS},
        SublimationPath::FURNACE,
        40,
        -3.0f,
        2.0f,
        OrganQuality::FLAWED,
        0.5f
    });

    // Radiance Aberrant Eye
    register_organ(DroppedOrganDef{
        "dropped_aberrant_radiance_eye",
        "Aberrant Radiance Eye",
        OrganSlot::EYE,
        BloodlineSource::ABERRATION,
        "radiance_aberrant",
        magic::RuneElement::LIGHT,
        {magic::RuneElement::ORDER},
        SublimationPath::RADIANCE,
        40,
        -3.0f,
        2.0f,
        OrganQuality::FLAWED,
        0.5f
    });
}

} // namespace science_and_theology::source_law
