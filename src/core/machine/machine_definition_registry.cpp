#include "machine_definition_registry.hpp"

namespace science_and_theology::gt {

std::unordered_map<std::string, MachineDefinition>&
MachineDefinitionRegistry::registry() {
    static std::unordered_map<std::string, MachineDefinition> g_registry;
    return g_registry;
}

void MachineDefinitionRegistry::initialize() {
    registry().clear();
}

bool MachineDefinitionRegistry::register_definition(
    const MachineDefinition& def) {
    if (def.type_key.empty()) return false;
    registry()[def.type_key] = def;
    return true;
}

const MachineDefinition* MachineDefinitionRegistry::get_definition(
    const std::string& type_key) {
    auto it = registry().find(type_key);
    return (it != registry().end()) ? &it->second : nullptr;
}

bool MachineDefinitionRegistry::has_definition(const std::string& type_key) {
    return registry().count(type_key) > 0;
}

size_t MachineDefinitionRegistry::get_definition_count() {
    return registry().size();
}

} // namespace science_and_theology::gt
