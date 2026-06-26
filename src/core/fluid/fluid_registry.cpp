#include "fluid_registry.hpp"

#include "common/string_pool.hpp"

#include <unordered_map>

namespace science_and_theology::gt {

static std::vector<FluidDefinition> g_fluid_registry;
static std::unordered_map<std::string, FluidId> g_fluid_name_map;

std::vector<FluidDefinition>& FluidRegistry::registry() {
    return g_fluid_registry;
}

void FluidRegistry::reset() {
    // 完全清空 registry（用于热重载），不预留 ID 0。
    g_fluid_registry.clear();
    g_fluid_name_map.clear();
}

void FluidRegistry::initialize() {
    // initialize = reset + reserve ID 0 as invalid.
    reset();

    // Reserve ID 0 as invalid.
    g_fluid_registry.push_back({"__invalid__", "Invalid", "", 0, false});

    // Built-in fluids are now registered from GDScript via GDFluidRegistry
    // (see BuiltinFluids.gd).
}

FluidId FluidRegistry::register_fluid(const FluidDefinition& def, FluidId explicit_id) {
    // Check for duplicates by name.
    auto name_it = g_fluid_name_map.find(def.name);
    if (name_it != g_fluid_name_map.end()) {
        return name_it->second;
    }

    // 强制显式 ID：不传 explicit_id 则拒绝注册（不再支持自动分配）
    if (explicit_id == kInvalidFluidId || explicit_id == 0) {
        return kInvalidFluidId;
    }
    FluidId id = explicit_id;

    // 显式 ID 可能跳跃，需要 resize 填补空隙
    if (id >= g_fluid_registry.size()) {
        g_fluid_registry.resize(static_cast<size_t>(id) + 1);
    }
    g_fluid_registry[id] = def;
    g_fluid_name_map[def.name] = id;
    return id;
}

const FluidDefinition* FluidRegistry::get_fluid(FluidId id) {
    if (id == kInvalidFluidId || id >= g_fluid_registry.size()) return nullptr;
    // 跳过空隙条目（显式 ID 留下的空 slot，name 为空）
    if (g_fluid_registry[id].name == nullptr || g_fluid_registry[id].name[0] == '\0') return nullptr;
    return &g_fluid_registry[id];
}

const FluidDefinition* FluidRegistry::get_fluid_by_name(const char* name) {
    auto it = g_fluid_name_map.find(name);
    if (it == g_fluid_name_map.end()) return nullptr;
    return get_fluid(it->second);
}

FluidId FluidRegistry::get_fluid_id(const char* name) {
    auto it = g_fluid_name_map.find(name);
    return (it != g_fluid_name_map.end()) ? it->second : kInvalidFluidId;
}

size_t FluidRegistry::get_fluid_count() {
    return g_fluid_name_map.size();
}

} // namespace science_and_theology::gt