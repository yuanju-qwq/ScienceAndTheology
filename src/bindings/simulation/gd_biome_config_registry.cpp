#include "gd_biome_config_registry.hpp"

#include <godot_cpp/core/class_db.hpp>

#include "core/simulation/ecosystem_params.hpp"
#include "core/simulation/ecosystem_system.hpp"

namespace science_and_theology {

using namespace godot;

void GDBiomeConfigRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDBiomeConfigRegistry",
        D_METHOD("register_biome_override", "def"),
        &GDBiomeConfigRegistry::register_biome_override);
}

bool GDBiomeConfigRegistry::register_biome_override(const Dictionary& def) {
    EcosystemParams::BiomeOverride bo;
    bo.biome_type = static_cast<uint8_t>(
        static_cast<int>(def.get("biome_type", 0)));
    bo.base_water = static_cast<float>(def.get("base_water", 0.5));
    bo.base_fertility = static_cast<float>(def.get("base_fertility", 0.5));
    bo.veg_growth_multiplier = static_cast<float>(
        def.get("veg_growth_multiplier", 1.0));
    bo.max_vegetation = static_cast<float>(def.get("max_vegetation", 1.0));
    bo.max_herbivore = static_cast<float>(def.get("max_herbivore", 1.0));
    bo.max_predator = static_cast<float>(def.get("max_predator", 1.0));

    return EcosystemSystem::register_biome_override(bo);
}

} // namespace science_and_theology
