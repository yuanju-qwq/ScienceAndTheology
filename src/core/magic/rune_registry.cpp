#include "rune_registry.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::magic {

static std::vector<RuneDef> g_rune_registry;
static std::vector<std::string> g_rune_name_storage;
static std::unordered_map<std::string, RuneId> g_rune_name_map;
static RuneId g_rune_index[static_cast<int>(RuneElement::COUNT)][static_cast<int>(RuneTier::COUNT)];

void RuneRegistry::initialize() {
    g_rune_registry.clear();
    g_rune_name_storage.clear();
    g_rune_name_map.clear();

    for (int e = 0; e < static_cast<int>(RuneElement::COUNT); ++e) {
        for (int t = 0; t < static_cast<int>(RuneTier::COUNT); ++t) {
            g_rune_index[e][t] = kInvalidRuneId;
        }
    }

    // Reserve ID 0 as invalid.
    g_rune_registry.push_back({});
    g_rune_name_storage.push_back("__invalid__");

    // Built-in runes are now registered from GDScript via GDRuneRegistry
    // (see BuiltinRunes.gd).
}

RuneId RuneRegistry::register_rune(const RuneDef& def) {
    if (g_rune_registry.size() >= kInvalidRuneId) return kInvalidRuneId;

    RuneDef stored = def;
    g_rune_name_storage.push_back(def.name);
    stored.name = g_rune_name_storage.back().c_str();

    RuneId id = static_cast<RuneId>(g_rune_registry.size());
    g_rune_registry.push_back(stored);
    g_rune_name_map[stored.name] = id;

    int e = static_cast<int>(stored.element);
    int t = static_cast<int>(stored.tier);
    g_rune_index[e][t] = id;
    return id;
}

const RuneDef* RuneRegistry::get_by_id(RuneId id) {
    if (id == kInvalidRuneId || id >= g_rune_registry.size()) return nullptr;
    return &g_rune_registry[id];
}

const RuneDef* RuneRegistry::get_by_name(const char* name) {
    auto it = g_rune_name_map.find(name);
    if (it == g_rune_name_map.end()) return nullptr;
    return get_by_id(it->second);
}

const RuneDef* RuneRegistry::get(RuneElement element, RuneTier tier) {
    int e = static_cast<int>(element);
    int t = static_cast<int>(tier);
    return get_by_id(g_rune_index[e][t]);
}

RuneId RuneRegistry::get_id(RuneElement element, RuneTier tier) {
    int e = static_cast<int>(element);
    int t = static_cast<int>(tier);
    return g_rune_index[e][t];
}

size_t RuneRegistry::count() {
    return g_rune_registry.size() > 0 ? g_rune_registry.size() - 1 : 0;
}

void RuneRegistry::register_builtin_runes() {
    // No-op: built-in runes are now registered from GDScript via
    // GDRuneRegistry (see BuiltinRunes.gd).
}

} // namespace science_and_theology::magic
