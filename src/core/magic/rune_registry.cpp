#include "rune_registry.hpp"

#include "core/common/string_pool.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::magic {

static std::vector<RuneDef> g_rune_registry;
static std::unordered_map<std::string, RuneId> g_rune_name_map;
static RuneId g_rune_index[static_cast<int>(RuneElement::COUNT)][static_cast<int>(RuneTier::COUNT)];

void RuneRegistry::reset() {
    // 完全清空 registry，不保留 ID 0（invalid）。
    // 用于热重载场景：initialize() = reset() + 预留 ID 0。
    g_rune_registry.clear();
    g_rune_name_map.clear();

    for (int e = 0; e < static_cast<int>(RuneElement::COUNT); ++e) {
        for (int t = 0; t < static_cast<int>(RuneTier::COUNT); ++t) {
            g_rune_index[e][t] = kInvalidRuneId;
        }
    }
}

void RuneRegistry::initialize() {
    // initialize = reset + 预留 ID 0 作为 invalid
    reset();

    // Reserve ID 0 as invalid.
    g_rune_registry.push_back({});

    // Built-in runes are now registered from GDScript via GDRuneRegistry
    // (see BuiltinRunes.gd).
}

RuneId RuneRegistry::register_rune(const RuneDef& def, RuneId explicit_id) {
    // 幂等：若同名 rune 已注册，直接返回已有 ID，不创建新条目
    auto it = g_rune_name_map.find(def.name);
    if (it != g_rune_name_map.end()) {
        return it->second;
    }

    // 强制显式 ID：不传 explicit_id 则拒绝注册（不再支持自动分配）
    if (explicit_id == kInvalidRuneId || explicit_id == 0) {
        return kInvalidRuneId;
    }
    RuneId id = explicit_id;

    // 显式 ID 可能跳跃，需要 resize 填补空隙（空隙条目保持默认构造，name="" ）
    if (id >= g_rune_registry.size()) {
        g_rune_registry.resize(static_cast<size_t>(id) + 1);
    }
    g_rune_registry[id] = def;
    g_rune_name_map[def.name] = id;

    int e = static_cast<int>(def.element);
    int t = static_cast<int>(def.tier);
    g_rune_index[e][t] = id;
    return id;
}

const RuneDef* RuneRegistry::get_by_id(RuneId id) {
    if (id == kInvalidRuneId || id >= g_rune_registry.size()) return nullptr;
    // 跳过空隙条目（显式 ID 留下的空 slot，name 为空）
    if (g_rune_registry[id].name == nullptr || g_rune_registry[id].name[0] == '\0') return nullptr;
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
    return g_rune_name_map.size();
}

} // namespace science_and_theology::magic
