#include "dropped_organ_registry.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::source_law {

static std::vector<DroppedOrganDef> g_dropped_organs;
static std::vector<std::string> g_dropped_organ_name_storage;
static std::unordered_map<std::string, DroppedOrganId> g_dropped_organ_name_map;

void DroppedOrganRegistry::reset() {
    // 清空所有全局变量（vector + map + 字符串池），不 reserve ID 0。
    g_dropped_organs.clear();
    g_dropped_organ_name_storage.clear();
    g_dropped_organ_name_map.clear();
}

void DroppedOrganRegistry::initialize() {
    reset();

    // Reserve ID 0 as invalid.
    g_dropped_organs.push_back({});
    g_dropped_organ_name_storage.push_back("__invalid__");

    // Built-in organs are now registered from GDScript via
    // GDDroppedOrganRegistry / BuiltinDroppedOrgans.
}

const DroppedOrganDef* DroppedOrganRegistry::get_by_id(DroppedOrganId id) {
    if (id == kInvalidDroppedOrganId || id >= g_dropped_organs.size()) return nullptr;
    // 跳过空隙条目（显式 ID 留下的空 slot，id 为空）
    if (g_dropped_organs[id].id == nullptr || g_dropped_organs[id].id[0] == '\0') return nullptr;
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
        // 跳过空隙条目（显式 ID 留下的空 slot）
        if (g_dropped_organs[i].id == nullptr || g_dropped_organs[i].id[0] == '\0') continue;
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
        // 跳过空隙条目（显式 ID 留下的空 slot）
        if (g_dropped_organs[i].id == nullptr || g_dropped_organs[i].id[0] == '\0') continue;
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
        // 跳过空隙条目（显式 ID 留下的空 slot）
        if (g_dropped_organs[i].id == nullptr || g_dropped_organs[i].id[0] == '\0') continue;
        if (g_dropped_organs[i].source == BloodlineSource::ABERRATION &&
            g_dropped_organs[i].imitated_path == path) {
            result.push_back(static_cast<DroppedOrganId>(i));
        }
    }
    return result;
}

size_t DroppedOrganRegistry::count() {
    return g_dropped_organ_name_map.size();
}

DroppedOrganId DroppedOrganRegistry::register_organ(const DroppedOrganDef& def,
                                                    DroppedOrganId explicit_id) {
    // 幂等：如果 def.id 已存在，直接返回已有 ID。
    auto it = g_dropped_organ_name_map.find(def.id);
    if (it != g_dropped_organ_name_map.end()) {
        return it->second;
    }

    // 强制显式 ID：不传 explicit_id 则拒绝注册（不再支持自动分配）
    if (explicit_id == kInvalidDroppedOrganId || explicit_id == 0) {
        return kInvalidDroppedOrganId;
    }
    DroppedOrganId id = explicit_id;

    DroppedOrganDef stored = def;

    // 先记录 push_back 前的下标，push_back 后用下标访问。
    // 连续 3 次 push_back 到同一个 vector 时，back().c_str() 会因扩容而失效，
    // 因此用下标方式稳定获取 c_str()。
    size_t id_idx = g_dropped_organ_name_storage.size();
    g_dropped_organ_name_storage.push_back(def.id);
    stored.id = g_dropped_organ_name_storage[id_idx].c_str();

    // Store title_key in stable storage.
    size_t title_idx = g_dropped_organ_name_storage.size();
    g_dropped_organ_name_storage.push_back(def.title_key);
    stored.title_key = g_dropped_organ_name_storage[title_idx].c_str();

    // Also store source_creature_id in stable storage.
    size_t creature_idx = g_dropped_organ_name_storage.size();
    g_dropped_organ_name_storage.push_back(def.source_creature_id);
    stored.source_creature_id = g_dropped_organ_name_storage[creature_idx].c_str();

    // 显式 ID 可能跳跃，需要 resize 填补空隙
    if (id >= g_dropped_organs.size()) {
        g_dropped_organs.resize(static_cast<size_t>(id) + 1);
    }
    g_dropped_organs[id] = stored;
    g_dropped_organ_name_map[stored.id] = id;
    return id;
}

} // namespace science_and_theology::source_law
